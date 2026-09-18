#define _GNU_SOURCE

#include "fh8626_kernel.h"
#include "fh8626_native_abi.h"
#include "native/h264/fh8626_h264_app_rc.h"
#include "native/h264/fh8626_h264_control.h"
#include "native/ae/fh8626_ae_runtime.h"
#include "native/awb_ccm/fh8626_stock_awb_mode0_pipeline.h"
#include "native/control/fh8626_control_status_tail.h"
#include "native/media/fh8626_geometry_linux.h"
#include "native/media/fh8626_media_timing.h"
#include "native/jpeg/fh8626_jpeg_config.h"
#include "native/sensor/gc1054/fh8626_sensor_gc1054_day_profile.h"
#include "../globals.h"
#include "../../app_config.h"
#include "../macros.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <sys/file.h>

#define FH8626_ISP_MMIO_PHYS 0xE8400000u
#define FH8626_ISP_MMIO_SIZE 0x4000u
#define FH8626_ISP_CFG_SIZE  0x1CE000u
#define FH8626_ISP_MASK      0x000FFFFFu
#define FH8626_SCRATCH_SIZE  (4u * 1024u * 1024u)
#define FH8626_ISP_NR3D_QUERY 0x80206926UL

struct fh8626_kernel {
    int media_fd, isp_fd, pae_fd, vmm_fd, mem_fd, jpeg_fd;
    struct fh8626_mem3 isp_cfg, vpu_sys, vpu_chn, vpu_chn_jpeg,
        pae_sys, pae_chn;
    volatile uint32_t *mmio;
    struct fh_sensor_gc1054 sensor;
    struct fh_isp_runtime isp_runtime;
    struct fh8626_ae_runtime ae_runtime;
    struct fhg_session geometry;
    struct fhg_session jpeg_geometry;
    struct fh8626_native_runtime runtime;
    struct fh8626_stream_backend stream;
    struct fh8626_native_adapter adapter;
    pthread_t thread;
    pthread_t control_thread;
    int thread_started;
    int control_thread_started;
    int media_bound;
    int pae_started;
    int pae_system_initialized;
    int pae_channel_initialized;
    int vpu_system_initialized;
    int vpu_enabled;
    uint32_t vpu_open_mask;
    volatile int running;
    fh8626_video_sink sink;
    uint8_t *scratch;
    struct fh8626_native_config config;
    int lock_fd;
    uint64_t pump_errors;
    int last_pump_error;
    uint32_t stats_epoch;
    uint32_t control_frames;
    uint8_t e2_stats[0x90];
    uint32_t e2_stats_epoch;
    uint32_t awb_stats_epoch;
    int e2_stats_valid;
    struct fh_stock_awb_mode0 awb_mode0;
    int awb_last_rc;
    uint32_t frame_status_retry;
    pthread_mutex_t jpeg_lock;
    pthread_mutex_t control_lock;
    int jpeg_lock_ready;
    int control_lock_ready;
    pthread_t jpeg_thread;
    int jpeg_thread_started;
    volatile int jpeg_running;
    int jpeg_ready;
    uint32_t jpeg_mode, jpeg_width, jpeg_height;
    uint8_t *jpeg_latest;
    size_t jpeg_latest_len;
    uint32_t jpeg_latest_width, jpeg_latest_height;
    struct {
        struct fh8626_mem3 mem;
        uint32_t mode, width, height, quality, fps, bitrate;
        int ready;
    } jpeg_slot[2];
};

static void *map_phys(int fd, size_t len, uint32_t phys)
{
#if defined(__arm__)
    void *p = (void *)(intptr_t)syscall(192, NULL, len, PROT_READ | PROT_WRITE,
                                        MAP_SHARED, fd, phys >> 12);
    return p == MAP_FAILED ? MAP_FAILED : p;
#else
    return mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, phys);
#endif
}

static int call_ioctl(int fd, unsigned long request, void *arg)
{
    int rc;

    errno = 0;
    rc = ioctl(fd, request, arg);
    if (rc == 0)
        return 0;
    return rc == -1 && errno ? -errno : -EIO;
}

static int open_required_device(const char *path, int flags, int *out)
{
    int fd;

    if (!path || !out)
        return -EINVAL;
    errno = 0;
    fd = open(path, flags);
    if (fd < 0) {
        int rc = errno ? -errno : -EIO;
        HAL_WARNING("fh8626", "required device %s open failed: %s\n",
            path, errno ? strerror(errno) : "unknown error");
        return rc;
    }
    *out = fd;
    return 0;
}

static int alloc_vmm(struct fh8626_kernel *k, const char *name, uint32_t need,
                     struct fh8626_mem3 *mem)
{
    uint8_t request[104];
    void *mapped;

    if (!k || !name || !need || !mem || mem->phys || mem->virt || mem->size)
        return -EINVAL;
    mem->size = (need + 4095u) & ~4095u;
    memset(request, 0, sizeof(request));
    memcpy(request + 8, &(uint32_t){4096u}, sizeof(uint32_t));
    memcpy(request + 12, &mem->size, sizeof(uint32_t));
    strncpy((char *)request + 28, name, 15);
    strncpy((char *)request + 44, "anonymous", 15);
    if (call_ioctl(k->vmm_fd, FH8626_VMM_ALLOC, request)) {
        memset(mem, 0, sizeof(*mem));
        return -EIO;
    }
    memcpy(&mem->phys, request, sizeof(uint32_t));
    mapped = map_phys(k->vmm_fd, mem->size, mem->phys);
    if (mapped == MAP_FAILED) {
        memset(mem, 0, sizeof(*mem));
        return -errno;
    }
    memset(mapped, 0, mem->size);
    mem->virt = (uint32_t)(uintptr_t)mapped;
    return 0;
}

static void free_mem(struct fh8626_mem3 *mem)
{
    if (!mem)
        return;
    if (mem->virt && mem->size)
        munmap((void *)(uintptr_t)mem->virt, mem->size);
    memset(mem, 0, sizeof(*mem));
}

static void isp_regs_720p(volatile uint32_t *regs)
{
    static const struct { uint16_t offset; uint32_t value; } init[] = {
        {0x008,0x00000000},{0x018,0x00000001},{0x024,0x0A61EB00},
        {0x068,0x00000005},{0x028,0x00002610},{0x0DC,0x00040000},
        {0x0E0,0x000C0008},{0x0E4,0x00140010},{0x0E8,0x001C0018},
        {0x0EC,0x00240020},{0x0F0,0x002C0028},{0x0F4,0x00400000},
        {0x0F8,0x00C00080},{0x0FC,0x01400100},{0x100,0x01C00180},
        {0x104,0x02400200},{0x108,0x02C00280},{0x10C,0x00100010},
        {0x110,0x00100010},{0x114,0x00100010},{0x118,0x00100010},
        {0x11C,0x00100010},{0x120,0x00100010},{0x1F0,0x00009015},
        {0x2F8,0x06420588},{0x2FC,0x07A706F7},{0x300,0x08FC0853},
        {0x304,0x0A4509A2},{0x308,0x0B840AE6},{0x30C,0x00000C1E},
        {0x030,0x02CF04FF},{0x078,0x02CF04FF},{0x080,0x02CF04FF},
        {0x178,0x00000044},{0x17C,0x00D40000},{0x180,0x027E01A9},
        {0x184,0x00770000},{0x188,0x016700EF},{0x1A4,0x00D40000},
        {0x1A8,0x027E01A9},{0x1AC,0x00770000},{0x1B0,0x016700EF},
        {0x1CC,0x0F0F1527},{0x1D0,0x00000000},{0x1D4,0x0001FFF1},
        {0x1DC,0x0F0F1527},{0x1E0,0x00000000},{0x1E4,0x0001FFF1},
        {0x1EC,0x15190000},{0x38C,0x00500000},{0x390,0x00F000A0},
        {0x394,0x01900140},{0x398,0x023001E0},{0x39C,0x0000027F},
        {0x3A0,0x00B4005A},{0x3A4,0x0167010E},{0x3A8,0x0FFF0000},
        {0x488,0x00000140},{0x490,0x00000640},{0x5C0,0x00000000},
        {0x5C4,0x02CF04FF}
    };
    size_t i;

    if (!regs)
        return;
    for (i = 0; i < sizeof(init) / sizeof(init[0]); ++i)
        regs[init[i].offset / 4u] = init[i].value;
}

static int geometry_enter(void *opaque, const struct fhg_plan *plan,
                          enum fhg_phase phase)
{
    struct fh8626_kernel *k = opaque;
    uint32_t wire[61];
    uint32_t channel;
    int rc;

    (void)plan;
    (void)phase;
    if (!k || k->isp_fd < 0)
        return -EINVAL;
    /* The vendor driver reports an uninitialized channel as this raw result.
     * Any other result means that another pipeline already owns the channel;
     * never overwrite it during native startup. */
    for (channel = 0; channel < 3; ++channel) {
        if (channel == 0 && k->geometry.state != FHG_STATE_FRESH)
            continue;
        memset(wire, 0, sizeof(wire));
        wire[0] = channel;
        errno = 0;
        rc = ioctl(k->isp_fd, FH8626_VPU_GET_CHN_MEM, wire);
        if ((uint32_t)rc != 0x80094081u) {
            HAL_WARNING("fh8626", "VPU channel %u is not fresh: rc=%#x\n",
                channel, (unsigned)rc);
            return -EBUSY;
        }
    }
    return 0;
}

static void geometry_leave(void *opaque)
{
    (void)opaque;
}

static int kernel_copy(void *opaque, uint8_t *dst, const uint8_t *src,
                       size_t len)
{
    (void)opaque;
    memcpy(dst, src, len);
    return 0;
}

static int kernel_ioctl_adapter(void *opaque, int fd, unsigned long request,
                                 void *arg)
{
    (void)opaque;
    return call_ioctl(fd, request, arg);
}

static int kernel_h264_ioctl(void *opaque, unsigned long request, void *arg)
{
    struct fh8626_kernel *k = opaque;

    if (!k || k->pae_fd < 0)
        return -ENODEV;
    return call_ioctl(k->pae_fd, request, arg);
}

static int kernel_ae_timing(void *opaque, uint32_t timing[4])
{
    struct fh8626_kernel *k = opaque;

    if (!k || !timing)
        return -EINVAL;
    return call_ioctl(k->isp_fd, FH8626_ISP_690E, timing);
}

static int kernel_ae_step(void *opaque)
{
    struct fh8626_kernel *k = opaque;
    uint32_t target;

    if (!k)
        return -EINVAL;
    target = (uint32_t)k->isp_runtime.ctx[0x30u] << 4;
    if (!target)
        target = 1280u;
    return fh8626_ae_runtime_step_metric(&k->ae_runtime,
        k->isp_runtime.c757c_metric_q12, target);
}

static void kernel_awb_init(void *opaque, const uint32_t triplet[3])
{
    struct fh8626_kernel *k = opaque;

    if (k && triplet)
        fh_stock_awb_init_triplet(&k->awb_mode0, triplet);
}

static int kernel_awb_step(void *opaque)
{
    struct fh8626_kernel *k = opaque;
    int rc;

    if (!k)
        return -EINVAL;
    if (!k->e2_stats_valid || k->awb_stats_epoch == k->e2_stats_epoch)
        return 0;
    k->awb_stats_epoch = k->e2_stats_epoch;
    /* The validated v4.3 owner enables normal AWB mode 1 by default.  The
     * shared dispatcher is the recovered CA4F4 -> CB4F0 -> CAFC0 -> C9F68
     * transaction; pausing mode 1 here skips the state that drives CCM. */
    k->awb_mode0.sensor_gain = fh_sensor_gc1054_awb_gain;
    k->awb_mode0.sensor_query = fh_sensor_gc1054_awb_query;
    k->awb_mode0.sensor_gain_opaque = &k->sensor;
    k->awb_mode0.mode1_paused = 0;
    rc = fh_stock_awb_dispatch(&k->awb_mode0, k->isp_runtime.ctx,
                               k->mmio, k->e2_stats, NULL);
    k->awb_last_rc = rc;
    return rc;
}

static int kernel_control_mask(void *opaque, uint32_t *mask)
{
    struct fh8626_kernel *k = opaque;

    if (!k || !mask)
        return -EINVAL;
    return call_ioctl(k->isp_fd, FH8626_ISP_692E, mask);
}

static int kernel_control_tail_awb(void *opaque)
{
    struct fh8626_kernel *k = opaque;

    if (!k)
        return -EINVAL;
    k->isp_runtime.last_control_tail_error = 0;
    if (!((k->isp_runtime.ctx[0x10] & 1u) &&
          (k->isp_runtime.ctx[0x2c] & 0x10u))) {
        (void)fh_control_status_tail(&k->isp_runtime,
            k->ae_runtime.gate.state, kernel_control_mask,
            kernel_ae_timing, k);
    }
    return kernel_awb_step(k);
}

static int kernel_nr3d_off(struct fh8626_kernel *k)
{
    struct { uint32_t mode, opaque[7]; } cfg;
    FILE *file;
    int rc;

    if (!k)
        return -EINVAL;
    file = fopen("/proc/driver/isp", "w");
    if (!file)
        return -errno;
    rc = fprintf(file, "nr3d_off\n") < 0 ? -EIO : 0;
    if (fclose(file) && !rc)
        rc = -errno;
    if (rc)
        return rc;
    memset(&cfg, 0, sizeof(cfg));
    rc = call_ioctl(k->isp_fd, FH8626_ISP_NR3D_QUERY, &cfg);
    if (rc)
        return rc;
    if (cfg.mode != 0u)
        return -EIO;
    fh_isp_runtime_set_nr3d_enabled(&k->isp_runtime, 0);
    return 0;
}

static int kernel_capture_awb_stats(struct fh8626_kernel *k)
{
    uint8_t first[0x90], second[0x90];
    volatile uint8_t *stats_base;
    unsigned i;
    uint32_t hash = 2166136261u;

    /* C64E0/C4244 rebind isp_runtime.isp_cfg to the completed descriptor
     * selected by 6905. C67C4 then consumes stats at selected root + 0x48.
     * Reading physical isp_cfg+0x1487b8 is correct only for bank zero. */
    if (!k || !k->isp_runtime.isp_cfg ||
        k->isp_runtime.isp_cfg_size < 0x48u + sizeof(first))
        return -ERANGE;
    stats_base = k->isp_runtime.isp_cfg + 0x48u;
    memcpy(first, (const void *)(uintptr_t)stats_base,
           sizeof(first));
    __sync_synchronize();
    memcpy(second, (const void *)(uintptr_t)stats_base,
           sizeof(second));
    if (memcmp(first, second, sizeof(first)))
        return -EAGAIN;
    for (i = 0; i < sizeof(first); ++i)
        hash = (hash ^ first[i]) * 16777619u;
    memcpy(k->e2_stats, first, sizeof(first));
    (void)hash;
    if (++k->e2_stats_epoch == 0u)
        k->e2_stats_epoch = 1u;
    k->e2_stats_valid = 1;
    return 0;
}

static int kernel_frontend_sync_barrier(struct fh8626_kernel *k)
{
    uint8_t ready_signal = 1u;
    uint32_t active = 0u;
    uint32_t timing[4] = {0u};
    uint32_t saved68;
    uint32_t delay_us = 80000u;

    if (!k)
        return -EINVAL;
    if (call_ioctl(k->isp_fd, FH8626_ISP_STATS_READY, &ready_signal) ||
        call_ioctl(k->isp_fd, FH8626_ISP_6933, &active))
        return -EIO;
    if (!active)
        return 0;
    if (ioctl(k->isp_fd, FH8626_ISP_690E, timing) == 0) {
        uint32_t divisor = timing[2] ? timing[2] : 1u;
        delay_us = 15000u + 1000u * (1000u / divisor);
    }
    memcpy(&saved68, k->isp_runtime.ctx + 0x68u, sizeof(saved68));
    saved68 &= ~1u;
    memcpy(k->isp_runtime.ctx + 0x68u, &saved68, sizeof(saved68));
    usleep(delay_us);
    memset(k->isp_runtime.ctx + 0x18u, 0, sizeof(uint32_t));
    usleep(delay_us);
    {
        uint32_t one = 1u;
        saved68 |= 1u;
        memcpy(k->isp_runtime.ctx + 0x18u, &one, sizeof(one));
        memcpy(k->isp_runtime.ctx + 0x68u, &saved68, sizeof(saved68));
    }
    return 0;
}

static int kernel_frame_status_gate(struct fh8626_kernel *k)
{
    int32_t status = 0;
    uint32_t timing[4] = {0u};

    if (!k)
        return -EINVAL;
    if (ioctl(k->isp_fd, FH8626_ISP_6930, &status) != 0) {
        k->frame_status_retry = 0u;
        return 0;
    }
    (void)ioctl(k->isp_fd, FH8626_ISP_690E, timing);
    if (status != -1) {
        k->frame_status_retry = 0u;
        return 0;
    }
    if (k->frame_status_retry < timing[2])
        ++k->frame_status_retry;
    else
        k->frame_status_retry = 0u;
    return 1;
}

static int kernel_control_tick(struct fh8626_kernel *k)
{
    uint32_t bank = 0u;
    int rc;

    if (!k)
        return -EINVAL;
    rc = kernel_frame_status_gate(k);
    if (rc < 0)
        return rc;
    if (rc > 0)
        return -EAGAIN;
    rc = call_ioctl(k->isp_fd, FH8626_ISP_6905, &bank);
    if (rc)
        return rc;
    if (bank != 0u && bank != 0x21270u) {
        HAL_WARNING("fh8626", "unexpected completed-bank offset %#x\\n", bank);
        return -ERANGE;
    }
    /* The per-frame runtime context is the owner-private 0x148770 view.
     * 0x127500 is the ioctl producer buffer, not the live control/stats
     * context consumed by AE/AWB.  Mixing the two yields valid-looking
     * frames but stale/invalid colour statistics. */
    if (k->isp_cfg.size <= 0x148770u + bank)
        return -ERANGE;
    rc = fh_isp_runtime_attach_isp_cfg(&k->isp_runtime,
        (volatile uint8_t *)(uintptr_t)k->isp_cfg.virt + 0x148770u + bank,
        k->isp_cfg.size - 0x148770u - bank);
    if (rc)
        return rc;
    rc = kernel_frontend_sync_barrier(k);
    if (rc)
        return rc;
    rc = kernel_capture_awb_stats(k);
    if (rc)
        return rc;
    /* The owner samples AE module +0x1f8 immediately before the shared
     * control epoch.  Native Divinus had the same AE runtime state but left
     * this bridge at reset zero, so the downstream control/ISP stages saw a
     * different default input even when AE itself was otherwise healthy. */
    k->isp_runtime.control_q8_aux = k->ae_runtime.q8_aux;
    if (++k->stats_epoch == 0u)
        k->stats_epoch = 1u;
    fh_isp_runtime_accept_stats_epoch(&k->isp_runtime, k->stats_epoch);
    rc = fh_isp_runtime_tick_with_control_hooks(&k->isp_runtime,
        kernel_ae_step, k, kernel_control_tail_awb, k);
    ++k->control_frames;
    if (k->control_frames <= 8u) {
        fprintf(stderr,
            "[fh8626] control=%u bank=%#x e2=%u awb_rc=%d tick_rc=%d "
            "ctx10=%02x ctx11=%02x ctx13=%02x ctx2c=%02x ctx30=%02x "
            "ctx6c=%02x ctx6d=%02x ctx6e=%02x ctx70=%08x "
            "awb224=%08x awb228=%08x reg4bc=%08x reg4c0=%08x\n",
            k->control_frames, bank, k->e2_stats_epoch, k->awb_last_rc, rc,
            k->isp_runtime.ctx[0x10u], k->isp_runtime.ctx[0x11u],
            k->isp_runtime.ctx[0x13u], k->isp_runtime.ctx[0x2cu],
            k->isp_runtime.ctx[0x30u],
            k->isp_runtime.ctx[0x6cu], k->isp_runtime.ctx[0x6du],
            k->isp_runtime.ctx[0x6eu], *(uint32_t *)(void *)(k->isp_runtime.ctx + 0x70u),
            k->mmio ? k->mmio[0x224u / sizeof(uint32_t)] : 0u,
            k->mmio ? k->mmio[0x228u / sizeof(uint32_t)] : 0u,
            k->mmio ? k->mmio[0x4bcu / sizeof(uint32_t)] : 0u,
            k->mmio ? k->mmio[0x4c0u / sizeof(uint32_t)] : 0u);
    }
    if (!rc && k->isp_runtime.ctx[0x11a8u] == 1u)
        (void)call_ioctl(k->isp_fd, FH8626_ISP_6924,
                         k->isp_runtime.ctx + 0x0f20u);
    return rc;
}

static int gc1054_format_for_fps(uint32_t fps, uint32_t *format)
{
    if (!format)
        return -EINVAL;
    switch (fps) {
    case 15u: *format = FH8626_GC1054_FORMAT_720P15; return 0;
    case 20u: *format = FH8626_GC1054_FORMAT_720P20; return 0;
    case 25u: *format = FH8626_GC1054_FORMAT_720P25; return 0;
    case 30u: *format = FH8626_GC1054_FORMAT_720P30; return 0;
    default: return -ENOTSUP;
    }
}

static int kernel_hal_init(void *opaque)
{
    struct fh8626_kernel *k = opaque;
    uint8_t vi_attr[24];
    uint32_t sensor_format;
    uint32_t mirror_flip;
    uint32_t bayer;
    void *mapped;
    int rc;

    rc = open_required_device("/dev/media_process", O_RDWR | O_CLOEXEC,
        &k->media_fd);
    if (rc)
        return rc;
    rc = open_required_device("/dev/isp", O_RDWR | O_CLOEXEC, &k->isp_fd);
    if (rc)
        return rc;
    rc = open_required_device("/dev/pae", O_RDWR | O_CLOEXEC, &k->pae_fd);
    if (rc)
        return rc;
    rc = open_required_device("/dev/vmm_userdev", O_RDWR | O_CLOEXEC,
        &k->vmm_fd);
    if (rc)
        return rc;
    rc = open_required_device("/dev/mem", O_RDWR | O_SYNC | O_CLOEXEC,
        &k->mem_fd);
    if (rc)
        return rc;

    errno = 0;
    k->lock_fd = open("/var/run/fh8626-divinus.lock",
        O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (k->lock_fd < 0) {
        rc = errno ? -errno : -EIO;
        HAL_WARNING("fh8626", "lock file open failed: %s\n",
            errno ? strerror(errno) : "unknown error");
        return rc;
    }
    if (flock(k->lock_fd, LOCK_EX | LOCK_NB) < 0)
        return errno == EWOULDBLOCK ? -EBUSY : -errno;

    rc = fh_sensor_gc1054_open(&k->sensor);
    if (rc)
        return rc;
    rc = fh_sensor_gc1054_init(&k->sensor);
    if (rc)
        return rc;
    rc = gc1054_format_for_fps(k->config.fps, &sensor_format);
    if (rc)
        return rc;
    rc = fh_sensor_gc1054_set_fmt(&k->sensor, sensor_format);
    if (rc)
        return rc;

    mirror_flip = (k->config.mirror ? 2u : 0u) |
                  (k->config.flip ? 1u : 0u);
    rc = fh_sensor_gc1054_set_mirror_flip(&k->sensor, mirror_flip);
    if (rc)
        return rc;
    rc = fh_sensor_gc1054_bayer_for_mirror_flip(mirror_flip, &bayer);
    if (rc)
        return rc;

    /* The validated owner waits for the sensor/MIPI block to settle after
     * Sensor_Init + set-format before reading VI attributes.  Reading the
     * callback immediately is accepted by the library but can leave the
     * downstream VPU with a stale sensor timing description. */
    usleep(600000u);
    memset(vi_attr, 0, sizeof(vi_attr));
    rc = fh_sensor_gc1054_get_vi_attr(&k->sensor, vi_attr);
    if (rc)
        return rc;

    fh_isp_runtime_reset(&k->isp_runtime);
    k->isp_runtime.awb_init = kernel_awb_init;
    k->isp_runtime.awb_init_opaque = k;
    rc = fh_isp_runtime_register_sensor(&k->isp_runtime, &k->sensor);
    if (rc)
        return rc;
    mapped = map_phys(k->mem_fd, FH8626_ISP_MMIO_SIZE, FH8626_ISP_MMIO_PHYS);
    if (mapped == MAP_FAILED)
        return -errno;
    k->mmio = mapped;
    rc = fh_isp_runtime_attach_mmio(&k->isp_runtime, mapped,
                                    FH8626_ISP_MMIO_SIZE);
    if (rc)
        return rc;
    rc = fh_isp_runtime_apply_vi_attr(&k->isp_runtime, vi_attr,
                                      sizeof(vi_attr));
    if (rc)
        return rc;

    /*
     * SetMirrorAndflipEx-equivalent ownership: sensor orientation and ISP
     * Bayer phase must advance together. The shared context is the owner;
     * apply_format_bits performs the exact masked MMIO publication.
     */
    return fh_isp_runtime_set_bayer_selector(&k->isp_runtime, bayer);
}

static int load_profile(struct fh8626_kernel *k)
{
    if (!k)
        return -EINVAL;

    /*
     * The retained 0xA58 day object is already the raw ISP parameter payload.
     * Do not pass it through the SREG-container parser.
     */
    return fh_isp_runtime_load_param(&k->isp_runtime,
        fh8626_gc1054_day_profile, FH8626_GC1054_DAY_PROFILE_SIZE);
}

static int kernel_system_init(void *opaque)
{
    struct fh8626_kernel *k = opaque;
    uint32_t mode = 0, q = 0, vi[3] = {0};
    uint8_t icfg[92];
    int rc;

    vi[0] = FH8626_NATIVE_WIDTH;
    vi[1] = FH8626_NATIVE_HEIGHT;

    rc = alloc_vmm(k, "isp_cfg", 0x1CE000u, &k->isp_cfg);
    if (rc)
        return rc;
    if (k->isp_cfg.size <= 0x148770u)
        return -ERANGE;
    rc = fh_isp_runtime_attach_isp_cfg(&k->isp_runtime,
        (volatile uint8_t *)(uintptr_t)k->isp_cfg.virt + 0x148770u,
        k->isp_cfg.size - 0x148770u);
    if (rc)
        return rc;
    /* Owner queries the real driver mode before ISP_6920. Preserve the
     * returned 0x20-byte eligibility record; a failed query invalidates it
     * but does not block the independent NR3D-off bring-up path. */
    {
        struct { uint32_t mode, opaque[7]; } nr3d = {0};
        if (!call_ioctl(k->isp_fd, FH8626_ISP_NR3D_QUERY, &nr3d))
            memcpy(k->isp_runtime.ctx + 0x11acu, &nr3d, sizeof(nr3d));
        else
            memset(k->isp_runtime.ctx + 0x11acu, 0, sizeof(nr3d));
    }
    memset(icfg, 0, sizeof(icfg));
    memcpy(icfg + 0x08, &(uint32_t){k->isp_cfg.phys + 0x127500u}, 4);
    memcpy(icfg + 0x28, &k->isp_cfg.phys, 4);
    memcpy(icfg + 0x48, &(uint32_t){k->isp_cfg.phys + 0x119400u}, 4);
    if (call_ioctl(k->isp_fd, FH8626_ISP_6921, &mode) ||
        call_ioctl(k->isp_fd, FH8626_ISP_6920, icfg) ||
        call_ioctl(k->isp_fd, FH8626_ISP_692F, &q) ||
        call_ioctl(k->isp_fd, FH8626_ISP_6919, NULL) ||
        call_ioctl(k->isp_fd, FH8626_ISP_6932, NULL))
        return -EIO;
    /* The owner clears this producer gate before VPU system memory setup as
     * well as after VPU_ENABLE. Preserve both lifecycle edges. */
    if (k->mmio) {
        k->mmio[0x008u / sizeof(uint32_t)] = 0u;
        __sync_synchronize();
    }
    rc = load_profile(k);
    if (rc)
        return rc;
    isp_regs_720p(k->mmio);
    if (fh_isp_runtime_apply_known_stock_init(&k->isp_runtime,
            FH8626_NATIVE_WIDTH, FH8626_NATIVE_HEIGHT))
        return -EIO;
    fprintf(stderr,
        "[fh8626] init ctx10=%02x ctx11=%02x ctx13=%02x ctx2c=%02x "
        "ctx30=%02x mmio024=%08x mmio224=%08x mmio228=%08x "
        "mmio4bc=%08x mmio4c0=%08x\n",
        k->isp_runtime.ctx[0x10u], k->isp_runtime.ctx[0x11u],
        k->isp_runtime.ctx[0x13u], k->isp_runtime.ctx[0x2cu],
        k->isp_runtime.ctx[0x30u], k->mmio[0x24u / sizeof(uint32_t)],
        k->mmio[0x224u / sizeof(uint32_t)], k->mmio[0x228u / sizeof(uint32_t)],
        k->mmio[0x4bcu / sizeof(uint32_t)], k->mmio[0x4c0u / sizeof(uint32_t)]);
    /* Exact owner ordering: re-arm the producer gate after the stock ISP
     * context has been published and immediately before VPU_MEM_QUERY. */
    if (k->mmio) {
        k->mmio[0x008u / sizeof(uint32_t)] = 0u;
        __sync_synchronize();
    }
    q = 0;
    if (call_ioctl(k->isp_fd, FH8626_VPU_MEM_QUERY, &q))
        return -EIO;
    rc = alloc_vmm(k, "vpu_sys", q, &k->vpu_sys);
    if (rc)
        return rc;
    rc = call_ioctl(k->isp_fd, FH8626_VPU_SYS_MEM_INIT, &k->vpu_sys);
    if (rc)
        return rc;
    k->vpu_system_initialized = 1;
    rc = call_ioctl(k->isp_fd, FH8626_VPU_SET_VI_ATTR, vi);
    if (rc)
        return rc;
    return 0;
}

static int kernel_pipeline_create(void *opaque)
{
    struct fh8626_kernel *k = opaque;
    struct fhg_request request = FHG_REQUEST_INITIALIZER;
    struct fhg_linux_context context;
    struct fhg_ops ops;
    struct fhg_requirements requirements;
    struct fhg_memory memory;
    struct fhg_error error;
    int rc;

    request.channel = 0;
    request.native_width = FH8626_NATIVE_WIDTH;
    request.native_height = FH8626_NATIVE_HEIGHT;
    request.visible_width = k->config.width;
    request.visible_height = k->config.height;
    request.capacity_width = k->config.width;
    request.capacity_height = k->config.height;
    request.coefficient = FHG_COEFF_INHERIT;
    context = (struct fhg_linux_context){k->isp_fd, k, geometry_enter,
                                         geometry_leave};
    rc = fhg_linux_make_ops(&context, &ops);
    if (rc)
        return rc;
    rc = fhg_query_requirements(&ops, &request, &requirements, &error);
    if (rc)
        return rc;
    rc = alloc_vmm(k, "vpu_chn0", requirements.bytes, &k->vpu_chn);
    if (rc)
        return rc;
    memory = (struct fhg_memory){k->vpu_chn.phys, k->vpu_chn.virt,
                                 k->vpu_chn.size};
    rc = fhg_configure_new_channel(&ops, &request, &memory,
                                   &k->geometry, &error);
    if (rc)
        return rc;

    if (app_config.jpeg_enable || app_config.mjpeg_enable) {
        uint32_t width = app_config.jpeg_enable ? app_config.jpeg_width : 0u;
        uint32_t height = app_config.jpeg_enable ? app_config.jpeg_height : 0u;
        if (app_config.mjpeg_enable && app_config.mjpeg_width > width) {
            width = app_config.mjpeg_width;
            height = app_config.mjpeg_height;
        }
        request.channel = 1u;
        request.visible_width = width;
        request.visible_height = height;
        request.capacity_width = width;
        request.capacity_height = height;
        rc = fhg_query_requirements(&ops, &request, &requirements, &error);
        if (rc)
            return rc;
        rc = alloc_vmm(k, "vpu_chn1_jpeg", requirements.bytes,
                       &k->vpu_chn_jpeg);
        if (rc)
            return rc;
        memory = (struct fhg_memory){k->vpu_chn_jpeg.phys,
            k->vpu_chn_jpeg.virt, k->vpu_chn_jpeg.size};
        rc = fhg_configure_new_channel(&ops, &request, &memory,
                                       &k->jpeg_geometry, &error);
        if (rc)
            return rc;
    }
    return 0;
}

static int kernel_video_destroy(void *opaque);

static int kernel_video_create(void *opaque)
{
    struct fh8626_kernel *k = opaque;
    struct fh8626_pae_mem_query query;
    struct fh8626_pae_mem memory;
    struct fh8626_pae_cfg config;
    struct fh_pae_rc_wire rc_config;
    uint32_t channel = 0, need = 0;
    int rc;

    if (call_ioctl(k->isp_fd, FH8626_VPU_OPEN_CHN, &channel))
        return -EIO;
    k->vpu_open_mask |= 1u << 0;
    if (k->vpu_chn_jpeg.phys) {
        channel = 1u;
        if (call_ioctl(k->isp_fd, FH8626_VPU_OPEN_CHN, &channel)) {
            rc = -EIO;
            goto fail;
        }
        k->vpu_open_mask |= 1u << 1;
    }
    {
        uint32_t pace[2] = {0u, fh8626_fps_packed(k->config.fps)};
        uint32_t readback[2] = {0u, 0u};
        if (call_ioctl(k->isp_fd, FH8626_VPU_SET_FRAMECTRL, pace) ||
            call_ioctl(k->isp_fd, FH8626_VPU_GET_FRAMECTRL, readback) ||
            readback[0] != pace[0] || readback[1] != pace[1]) {
            rc = -EIO;
            goto fail;
        }
    }
    if (call_ioctl(k->isp_fd, FH8626_ISP_START, NULL)) {
        rc = -EIO;
        goto fail;
    }
    if (fh_isp_runtime_apply_profile_luts(&k->isp_runtime) ||
        fh_isp_runtime_tick_proven_subset(&k->isp_runtime)) {
        rc = -EIO;
        goto fail;
    }

    /*
     * Stock VI/VPSS ownership enables the actual VPU channel before VENC
     * channel creation/start. 0xC004694D consumes the channel id itself;
     * channel 0 must therefore send payload 0, not a boolean 1.
     */
    channel = 0u;
    rc = call_ioctl(k->isp_fd, FH8626_VPU_ENABLE, &channel);
    if (rc)
        goto fail;
    k->vpu_enabled = 1;

    /* Stock clears the producer gate immediately after VPSS Enable. */
    if (k->mmio) {
        k->mmio[0x008u / sizeof(uint32_t)] = 0u;
        __sync_synchronize();
    }

    /* Keep the validated default NR3D state tied to producer enable. */
    rc = kernel_nr3d_off(k);
    if (rc)
        goto fail;
    fh8626_ae_runtime_init_passive(&k->ae_runtime, k->isp_runtime.ctx,
        k->mmio, &k->sensor, NULL, NULL, kernel_ae_timing, k);
    if (fh8626_ae_runtime_enable_observe(&k->ae_runtime, 1) ||
        fh8626_ae_runtime_enable_commit(&k->ae_runtime, 1)) {
        rc = -EIO;
        goto fail;
    }
    if (call_ioctl(k->pae_fd, FH8626_PAE_SYS_QUERY, &need)) {
        rc = -EIO;
        goto fail;
    }
    rc = alloc_vmm(k, "pae_sys", need, &k->pae_sys);
    if (rc)
        goto fail;
    if (call_ioctl(k->pae_fd, FH8626_PAE_SYS_INIT, &k->pae_sys)) {
        rc = -EIO;
        goto fail;
    }
    k->pae_system_initialized = 1;
    memset(&query, 0, sizeof(query));
    query.chn = 0;
    query.width = k->config.width;
    query.height = k->config.height;
    if (call_ioctl(k->pae_fd, FH8626_PAE_ENC_MEM_SIZE, &query)) {
        rc = -EIO;
        goto fail;
    }
    rc = alloc_vmm(k, "pae_enc0", query.size, &k->pae_chn);
    if (rc)
        goto fail;
    memory = (struct fh8626_pae_mem){0, k->pae_chn.phys, k->pae_chn.virt,
                                     k->pae_chn.size, k->config.width,
                                     k->config.height, 0};
    if (call_ioctl(k->pae_fd, FH8626_PAE_ENC_MEM_INIT, &memory)) {
        rc = -EIO;
        goto fail;
    }
    k->pae_channel_initialized = 1;
    /* FH_PAE_CFG field0c is the encoder's fixed input quantum, not GOP.
     * The recovered fixed FH8626 contract uses H.264 Baseline (profile id 66).
     * Divinus rejects non-baseline/non-25-GOP requests at the HAL boundary
     * instead of carrying ignored profile/GOP fields into this backend. */
    config = (struct fh8626_pae_cfg){0, k->config.width, k->config.height,
                                     50, k->config.profile, 28,
                                     fh8626_fps_packed(k->config.fps),
                                     0, 0, 0, 0};
    if (call_ioctl(k->pae_fd, FH8626_PAE_SET_CONFIG, &config)) {
        rc = -EIO;
        goto fail;
    }
    memset(&rc_config, 0, sizeof(rc_config));
    rc_config.chn = 0;
    rc_config.rc_mode = k->config.rc_mode;
    rc_config.frame_rate_packed = fh8626_fps_packed(k->config.fps);
    rc_config.init_qp = 38;
    if (k->config.bitrate_kbps > UINT32_MAX / 1000u) {
        rc = -ERANGE;
        goto fail;
    }
    rc_config.bitrate_or_rate = k->config.bitrate_kbps * 1000u;
    rc_config.i_min_qp = 30; rc_config.i_max_qp = 50;
    rc_config.p_min_qp = 30; rc_config.p_max_qp = 50;
    rc_config.i_proportion = 5; rc_config.p_proportion = 1;
    rc_config.still_rate_percent = 30; rc_config.max_rate_percent = 120;
    rc_config.ip_qp_delta = 3;
    rc_config.max_still_qp = 38;
    if (fh_pae_rc_validate_driver(&rc_config) ||
        call_ioctl(k->pae_fd, FH_PAE_SET_RC_CONFIG, &rc_config)) {
        rc = -EIO;
        goto fail;
    }
    return 0;

fail:
    {
        int rollback_rc = kernel_video_destroy(k);
        if (!rc)
            rc = rollback_rc;
    }
    return rc;
}

static int kernel_stream_thread_running(struct fh8626_kernel *k)
{
    return k->running && keepRunning;
}

static void *kernel_stream_thread(void *opaque)
{
    struct fh8626_kernel *k = opaque;
    int producer_probe_done = 0;
    while (kernel_stream_thread_running(k)) {
        struct timespec now;
        int rc;

        /* Descriptor word 10 is retained as raw evidence but its unit is not
         * established. Give Divinus an owned microsecond media clock from
         * monotonic dequeue time rather than pretending the raw word is us or
         * synthesizing timestamps solely from configured FPS. */
        if (!clock_gettime(CLOCK_MONOTONIC, &now))
            k->adapter.pts_us = (uint64_t)now.tv_sec * 1000000u +
                (uint64_t)now.tv_nsec / 1000u;

        rc = fh8626_native_adapter_pump(&k->adapter, k->sink);
        if (rc) {
            k->pump_errors++;
            if (!producer_probe_done && rc == -EIO) {
                uint32_t pae_status[12] = {0u};
                int status_rc = ioctl(k->pae_fd, FH8626_PAE_GET_STATUS,
                                      pae_status);
                HAL_WARNING("fh8626",
                    "producer probe: stream_rc=%#x status_rc=%d "
                    "pae=%08x/%08x/%08x/%08x mmio004=%08x mmio008=%08x "
                    "mmio024=%08x mmio030=%08x\\n",
                    rc, status_rc, pae_status[0], pae_status[1],
                    pae_status[2], pae_status[3],
                    k->mmio ? k->mmio[0x004u / sizeof(uint32_t)] : 0u,
                    k->mmio ? k->mmio[0x008u / sizeof(uint32_t)] : 0u,
                    k->mmio ? k->mmio[0x024u / sizeof(uint32_t)] : 0u,
                    k->mmio ? k->mmio[0x030u / sizeof(uint32_t)] : 0u);
                producer_probe_done = 1;
            }
            if (rc != k->last_pump_error || (k->pump_errors % 1000u) == 1u) {
                HAL_WARNING("fh8626", "native stream pump error %#x (count %llu)\\n",
                    rc, (unsigned long long)k->pump_errors);
                k->last_pump_error = rc;
            }
        }
        if (rc && rc != -EAGAIN)
            usleep(1000);
    }
    return NULL;
}

static void *kernel_control_thread(void *opaque)
{
    struct fh8626_kernel *k = opaque;
    struct timespec deadline;

    clock_gettime(CLOCK_MONOTONIC, &deadline);
    while (kernel_stream_thread_running(k)) {
        int rc;
        struct timespec now;

        pthread_mutex_lock(&k->control_lock);
        rc = kernel_control_tick(k);
        pthread_mutex_unlock(&k->control_lock);

        if (rc != -EAGAIN && rc) {
            k->pump_errors++;
            if (rc != k->last_pump_error || (k->pump_errors % 1000u) == 1u) {
                HAL_WARNING("fh8626", "native control error %#x (count %llu)\\n",
                    rc, (unsigned long long)k->pump_errors);
                k->last_pump_error = rc;
            }
        }
        deadline.tv_nsec += 40000000L;
        if (deadline.tv_nsec >= 1000000000L) {
            ++deadline.tv_sec;
            deadline.tv_nsec -= 1000000000L;
        }
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (deadline.tv_sec < now.tv_sec ||
            (deadline.tv_sec == now.tv_sec && deadline.tv_nsec < now.tv_nsec))
            deadline = now;
        (void)clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, NULL);
    }
    return NULL;
}

static int kernel_stage_stop(void *opaque);

static int kernel_stream_start(void *opaque)
{
    struct fh8626_kernel *k = opaque;
    uint32_t bind[2] = {1, 7};
    uint32_t channel = 0;
    int rc;

    /*
     * Stock VENC startup owns StartRecvPic before SYS BindVpu2Enc.
     * VPSS/VPU Enable is already owned by kernel_video_create().
     */
    rc = call_ioctl(k->pae_fd, FH8626_PAE_ENC_START, &channel);
    if (rc)
        return rc;
    k->pae_started = 1;

    rc = call_ioctl(k->media_fd, FH8626_MEDIA_BIND, bind);
    if (rc)
        goto fail;
    k->media_bound = 1;

    k->scratch = malloc(FH8626_SCRATCH_SIZE);
    if (!k->scratch) {
        rc = -ENOMEM;
        goto fail;
    }
    rc = fh8626_stream_backend_init(&k->stream, k->media_fd, k->pae_fd,
        k->pae_sys.virt, k->pae_sys.size, kernel_ioctl_adapter, k,
        &k->runtime.life);
    if (rc)
        goto fail;
    rc = fh8626_native_adapter_init(&k->adapter, &k->stream, k->scratch,
        FH8626_SCRATCH_SIZE, kernel_copy, k,
        fh8626_frame_interval_us(k->config.fps));
    if (rc)
        goto fail;

    /* Owner's run transition acknowledges pending ISP status and then
     * enables the producer interrupt mask. Leaving 0x008 cleared keeps the
     * VPU enabled but prevents any frame from reaching the PAE ring. */
    if (k->mmio) {
        uint32_t pending = k->mmio[0x004u / sizeof(uint32_t)];
        if (pending)
            k->mmio[0x004u / sizeof(uint32_t)] = pending;
        k->mmio[0x008u / sizeof(uint32_t)] = FH8626_ISP_MASK;
        __sync_synchronize();
    }

    k->running = 1;
    rc = pthread_create(&k->thread, NULL, kernel_stream_thread, k);
    if (rc) {
        k->running = 0;
        rc = -rc;
        goto fail;
    }
    k->thread_started = 1;

    rc = pthread_create(&k->control_thread, NULL, kernel_control_thread, k);
    if (rc) {
        rc = -rc;
        goto fail;
    }
    k->control_thread_started = 1;
    return 0;

fail:
    {
        int stop_rc = kernel_stage_stop(k);
        if (!rc)
            rc = stop_rc;
    }
    return rc;
}

static void kernel_quiesce_threads(struct fh8626_kernel *k)
{
    if (!k)
        return;

    k->running = 0;
    if (k->control_thread_started) {
        pthread_join(k->control_thread, NULL);
        k->control_thread_started = 0;
    }
    if (k->thread_started) {
        pthread_join(k->thread, NULL);
        k->thread_started = 0;
    }
}

static int kernel_stage_stop(void *opaque)
{
    struct fh8626_kernel *k = opaque;
    uint32_t zero = 0u;
    uint32_t source = 1u;
    int first_error = 0;
    int rc;

    kernel_quiesce_threads(k);

    /* Reverse stock StartRecvPic -> Bind ownership: unbind first. */
    if (k->media_bound && k->media_fd >= 0) {
        rc = call_ioctl(k->media_fd, FH8626_MEDIA_UNBIND_SRC, &source);
        if (rc) {
            if (!first_error)
                first_error = rc;
        } else {
            k->media_bound = 0;
        }
    }

    if (k->pae_started && k->pae_fd >= 0) {
        rc = call_ioctl(k->pae_fd, FH_PAE_STOP_RECV, &zero);
        if (rc) {
            if (!first_error)
                first_error = rc;
        } else {
            k->pae_started = 0;
        }
    }

    return first_error;
}

static int kernel_video_destroy(void *opaque)
{
    struct fh8626_kernel *k = opaque;
    int first_error = 0;
    int rc;
    int channel;

    if (!k)
        return -EINVAL;

    /*
     * PAE_RECYCLE_CHN is the driver-owned channel destructor. It performs
     * stop -> encoder flush -> stream flush, returns the 3-word VMM region,
     * unmaps internal buffers, unregisters media object channel+7 and clears
     * the complete encoder-channel state.
     */
    if (k->pae_channel_initialized && k->pae_fd >= 0) {
        uint32_t recycle[4] = {FH8626_NATIVE_CHANNEL, 0u, 0u, 0u};

        rc = call_ioctl(k->pae_fd, FH8626_PAE_RECYCLE_CHN, recycle);
        if (rc) {
            if (!first_error)
                first_error = rc;
        } else {
            k->pae_channel_initialized = 0;
            k->pae_started = 0;
        }
    }

    /*
     * 0x694E is the distinct no-payload vpu_disable() operation. Never model
     * disable as VPU_ENABLE with a zero payload.
     */
    if (k->vpu_enabled && k->isp_fd >= 0) {
        rc = call_ioctl(k->isp_fd, FH8626_VPU_DISABLE, NULL);
        if (rc) {
            if (!first_error)
                first_error = rc;
        } else {
            k->vpu_enabled = 0;
        }
    }

    /* Close configured VPSS channels in reverse open order. */
    for (channel = 1; channel >= 0; --channel) {
        uint32_t ch = (uint32_t)channel;
        if (!(k->vpu_open_mask & (1u << ch)) || k->isp_fd < 0)
            continue;
        rc = call_ioctl(k->isp_fd, FH8626_VPU_CLOSE_CHN, &ch);
        if (rc) {
            if (!first_error)
                first_error = rc;
        } else {
            k->vpu_open_mask &= ~(1u << ch);
        }
    }

    return first_error;
}

static int kernel_system_deinit(void *opaque)
{
    struct fh8626_kernel *k = opaque;
    int first_error = 0;
    int rc;

    if (!k)
        return -EINVAL;

    /*
     * Both system-uninit ioctls return their original 3-word user VMM
     * descriptor and refuse to run while child channels remain live.
     */
    if (k->vpu_system_initialized && k->isp_fd >= 0) {
        struct fh8626_mem3 returned = {0};

        rc = call_ioctl(k->isp_fd, FH8626_VPU_SYS_UNINIT, &returned);
        if (rc) {
            if (!first_error)
                first_error = rc;
        } else {
            k->vpu_system_initialized = 0;
        }
    }

    if (k->pae_system_initialized && k->pae_fd >= 0) {
        struct fh8626_mem3 returned = {0};

        rc = call_ioctl(k->pae_fd, FH8626_PAE_SYS_UNINIT, &returned);
        if (rc) {
            if (!first_error)
                first_error = rc;
        } else {
            k->pae_system_initialized = 0;
        }
    }

    return first_error;
}

static int kernel_hal_deinit(void *opaque)
{
    struct fh8626_kernel *k = opaque;
    int first_error = 0;
    int rc;

    if (!k)
        return -EINVAL;

    /*
     * fh81_isp_release() performs the remaining ISP-core shutdown on close,
     * including producer disable, IRQ/MMIO cleanup and vpu_release().
     * Close driver owners before returning their backing MMZ allocations.
     */
    if (k->pae_fd >= 0) {
        if (close(k->pae_fd) < 0 && !first_error)
            first_error = -errno;
        k->pae_fd = -1;
    }
    if (k->isp_fd >= 0) {
        if (close(k->isp_fd) < 0 && !first_error)
            first_error = -errno;
        k->isp_fd = -1;
    }
    if (k->media_fd >= 0) {
        if (close(k->media_fd) < 0 && !first_error)
            first_error = -errno;
        k->media_fd = -1;
    }
    if (k->mem_fd >= 0) {
        if (close(k->mem_fd) < 0 && !first_error)
            first_error = -errno;
        k->mem_fd = -1;
    }

    /*
     * mmz_userdev cmd 0x0c is owner-scoped reset/free-all. Divinus owns a
     * private vmm_fd, so this releases exactly the allocations made by this
     * HAL instance after every userspace mapping has been unmapped.
     */
    free_mem(&k->pae_chn);
    free_mem(&k->pae_sys);
    free_mem(&k->vpu_chn_jpeg);
    free_mem(&k->vpu_chn);
    free_mem(&k->vpu_sys);
    free_mem(&k->isp_cfg);

    if (k->vmm_fd >= 0) {
        /*
         * mmz_userdev_ioctl validates both _IOC_SIZE (0x68) and a non-NULL
         * userspace pointer even though command 0x0c only uses the fd-owned
         * allocation list internally. Supply a real zeroed wire buffer.
         */
        uint8_t reset_wire[0x68] = {0};

        rc = call_ioctl(k->vmm_fd, FH8626_VMM_RESET_OWNER, reset_wire);
        if (rc && !first_error)
            first_error = rc;
        if (close(k->vmm_fd) < 0 && !first_error)
            first_error = -errno;
        k->vmm_fd = -1;
    }

    return first_error;
}

static int kernel_noop(void *opaque)
{
    (void)opaque;
    return 0;
}

static int jpeg_slot_index(uint32_t mode)
{
    return mode == FH8626_JPEG_MODE_SNAPSHOT ? 0 :
        mode == FH8626_JPEG_MODE_MJPEG ? 1 : -1;
}

static int kernel_jpeg_bind(struct fh8626_kernel *k, uint32_t mode)
{
    uint32_t bind[2] = {2u,
        mode == FH8626_JPEG_MODE_SNAPSHOT ? 16u : 27u};

    if (call_ioctl(k->media_fd, FH8626_MEDIA_BIND, bind))
        return -EIO;
    return 0;
}

static int kernel_jpeg_query(struct fh8626_kernel *k, uint32_t mode,
    hal_vidstream *stream, uint8_t **owned)
{
    uint32_t q[92] = {0};
    uintptr_t base, end, user;
    uint32_t len;
    hal_vidpack *pack;
    int rc;

    if (!k || !stream || !owned || k->jpeg_fd < 0)
        return -EINVAL;
    q[0] = mode == FH8626_JPEG_MODE_MJPEG ? FH8626_MJPEG_STREAM_MASK :
        FH8626_JPEG_STREAM_MASK;
    rc = ioctl(k->media_fd, FH8626_MEDIA_STREAM_6, q);
    if (rc < 0)
        return errno ? -errno : -EAGAIN;
    len = q[7];
    user = q[6];
    {
        int index = jpeg_slot_index(mode);
        if (index < 0 || !k->jpeg_slot[index].ready)
            return -ENODEV;
        base = (uintptr_t)k->jpeg_slot[index].mem.virt;
        end = base + k->jpeg_slot[index].mem.size;
        if (!len || len > k->jpeg_slot[index].mem.size ||
            (uintptr_t)user < base || (uintptr_t)user > end - len) {
            (void)ioctl(k->jpeg_fd, FH8626_JPEG_RELEASE, &mode);
            return -EBADMSG;
        }
        *owned = malloc(len);
        if (!*owned) {
            (void)ioctl(k->jpeg_fd, FH8626_JPEG_RELEASE, &mode);
            return -ENOMEM;
        }
        memcpy(*owned, (const void *)(uintptr_t)user, len);
    }
    memset(stream, 0, sizeof(*stream));
    pack = calloc(1, sizeof(*pack));
    if (!pack) {
        free(*owned);
        *owned = NULL;
        return -ENOMEM;
    }
    pack->data = *owned;
    pack->length = len;
    pack->offset = 0;
    pack->timestamp = ((uint64_t)q[9] << 32) | q[8];
    stream->pack = pack;
    stream->count = 1;
    rc = ioctl(k->jpeg_fd, FH8626_JPEG_RELEASE, &mode);
    if (rc < 0) {
        free(pack);
        free(*owned);
        *owned = NULL;
        stream->pack = NULL;
        return errno ? -errno : -EIO;
    }
    return 0;
}

static void *kernel_mjpeg_thread(void *opaque)
{
    struct fh8626_kernel *k = opaque;

    while (k->jpeg_running && kernel_stream_thread_running(k)) {
        hal_vidstream stream;
        uint8_t *owned = NULL;
        int rc;

        pthread_mutex_lock(&k->jpeg_lock);
        rc = kernel_jpeg_query(k, FH8626_JPEG_MODE_MJPEG, &stream, &owned);
        if (!rc && stream.pack && stream.pack->length) {
            uint8_t *latest = realloc(k->jpeg_latest, stream.pack->length);
            if (latest) {
                memcpy(latest, stream.pack->data, stream.pack->length);
                k->jpeg_latest = latest;
                k->jpeg_latest_len = stream.pack->length;
                k->jpeg_latest_width = k->jpeg_slot[1].width;
                k->jpeg_latest_height = k->jpeg_slot[1].height;
            }
        }
        pthread_mutex_unlock(&k->jpeg_lock);
        if (!rc) {
            if (k->sink && k->jpeg_running)
                (void)k->sink(1, &stream);
            free(stream.pack);
            free(owned);
        } else if (rc != -EAGAIN && rc != -ETIMEDOUT && rc != -EBADMSG) {
            usleep(1000u);
        } else {
            usleep(5000u);
        }
    }
    return NULL;
}

int fh8626_kernel_jpeg_init(struct fh8626_kernel *k, uint32_t mode,
    uint32_t width, uint32_t height, uint32_t quality, uint32_t fps,
    uint32_t bitrate, uint32_t rc_mode)
{
    uint32_t query[4] = {mode, width, height, 0u};
    uint32_t init[6];
    struct fh_jpeg_cfg_wire mjpeg_cfg;
    struct fh_jpeg_drop_wire drop_cfg;
    uint32_t snapshot_cfg[4];
    int index, rc;

    if (!k || !width || !height || !fps)
        return -EINVAL;
    index = jpeg_slot_index(mode);
    if (index < 0 || width > 2048u || height > 2048u || quality > 98u)
        return -EINVAL;
    pthread_mutex_lock(&k->jpeg_lock);
    if (k->jpeg_slot[index].ready) {
        pthread_mutex_unlock(&k->jpeg_lock);
        return 0;
    }
    if (k->jpeg_fd < 0)
        k->jpeg_fd = open("/dev/jpeg", O_RDWR | O_CLOEXEC);
    if (k->jpeg_fd < 0) {
        rc = -errno;
        goto fail;
    }
    errno = 0;
    rc = ioctl(k->jpeg_fd, FH8626_JPEG_MEM_QUERY, query);
    if (rc < 0) {
        rc = errno ? -errno : -EIO;
        goto fail;
    }
    if (!query[3] || query[3] > INT32_MAX) {
        rc = -ERANGE;
        goto fail;
    }
    rc = alloc_vmm(k, mode == FH8626_JPEG_MODE_SNAPSHOT ?
        "jpeg_snapshot" : "jpeg_mjpeg", query[3],
        &k->jpeg_slot[index].mem);
    if (rc)
        goto fail;
    init[0] = mode;
    init[1] = k->jpeg_slot[index].mem.phys;
    init[2] = k->jpeg_slot[index].mem.virt;
    init[3] = k->jpeg_slot[index].mem.size;
    init[4] = width;
    init[5] = height;
    rc = call_ioctl(k->jpeg_fd, FH8626_JPEG_MEM_INIT, init);
    if (rc)
        goto fail_mem;
    if (mode == FH8626_JPEG_MODE_SNAPSHOT) {
        /*
         * jpeg_set_chn_cfg is a four-word snapshot policy record:
         * QP selector, resize mode, speed, rotation. Geometry belongs to
         * MEM_INIT/VPSS and must not be duplicated here.
         */
        snapshot_cfg[0] = quality;
        snapshot_cfg[1] = 2u;
        snapshot_cfg[2] = 4u;
        snapshot_cfg[3] = 0u;
        rc = call_ioctl(k->jpeg_fd, FH8626_JPEG_SET_CHN_CFG, snapshot_cfg);
    } else {
        memset(&mjpeg_cfg, 0, sizeof(mjpeg_cfg));
        mjpeg_cfg.mode = mode;
        mjpeg_cfg.width = width;
        mjpeg_cfg.height = height;
        mjpeg_cfg.src_fps_packed = fh8626_fps_packed(k->config.fps);
        mjpeg_cfg.dst_fps_packed = fh8626_fps_packed(fps);
        mjpeg_cfg.qp = quality;
        if (bitrate > UINT32_MAX / 1000u) {
            rc = -ERANGE;
            goto fail_mem;
        }
        mjpeg_cfg.target_rate = bitrate * 1000u;
        mjpeg_cfg.min_qp = 0u;
        mjpeg_cfg.max_qp = FH_JPEG_QP_MAX;
        mjpeg_cfg.rotation = 0u;

        switch (rc_mode) {
        case HAL_VIDMODE_QP:
            mjpeg_cfg.rc_selector = 0u;
            break;
        case HAL_VIDMODE_VBR:
        case HAL_VIDMODE_CBR:
            /*
             * jpeg.ko exposes one adaptive-QP controller; rc_selector is
             * boolean at the kernel boundary. Divinus differentiates CBR from
             * VBR with the separately recovered frame/drop controller below.
             */
            mjpeg_cfg.rc_selector = 1u;
            break;
        default:
            rc = -ENOTSUP;
            goto fail_mem;
        }

        if (fh_jpeg_cfg_validate_sdk(&mjpeg_cfg)) {
            rc = -EINVAL;
            goto fail_mem;
        }
        rc = call_ioctl(k->jpeg_fd, FH8626_JPEG_MJPEG_SET_CFG, &mjpeg_cfg);
        if (!rc && rc_mode == HAL_VIDMODE_CBR) {
            rc = fh_jpeg_drop_build_safe(&drop_cfg,
                mjpeg_cfg.src_fps_packed, mjpeg_cfg.dst_fps_packed,
                quality, mjpeg_cfg.dst_fps_packed,
                120u, mjpeg_cfg.dst_fps_packed);
            if (!rc)
                rc = call_ioctl(k->jpeg_fd, FH_JPEG_SET_DROP_CFG, &drop_cfg);
        }
        if (!rc)
            rc = call_ioctl(k->jpeg_fd, FH8626_JPEG_START, NULL);
    }
    if (rc)
        goto fail_mem;
    rc = kernel_jpeg_bind(k, mode);
    if (rc)
        goto fail_mem;
    k->jpeg_ready = 1;
    k->jpeg_slot[index].mode = mode;
    k->jpeg_slot[index].width = width;
    k->jpeg_slot[index].height = height;
    k->jpeg_slot[index].quality = quality;
    k->jpeg_slot[index].fps = fps;
    k->jpeg_slot[index].bitrate = bitrate;
    k->jpeg_slot[index].ready = 1;
    if (mode == FH8626_JPEG_MODE_MJPEG && !k->jpeg_thread_started) {
        k->jpeg_running = 1;
        rc = pthread_create(&k->jpeg_thread, NULL, kernel_mjpeg_thread, k);
        if (rc) {
            k->jpeg_running = 0;
            k->jpeg_slot[index].ready = 0;
            free_mem(&k->jpeg_slot[index].mem);
            goto fail;
        }
        k->jpeg_thread_started = 1;
    }
    pthread_mutex_unlock(&k->jpeg_lock);
    return 0;

fail_mem:
    free_mem(&k->jpeg_slot[index].mem);
fail:
    if (!k->jpeg_slot[0].ready && !k->jpeg_slot[1].ready &&
        k->jpeg_fd >= 0) {
        close(k->jpeg_fd);
        k->jpeg_fd = -1;
    }
    pthread_mutex_unlock(&k->jpeg_lock);
    return rc;
}

static int kernel_jpeg_snapshot_get(struct fh8626_kernel *k, uint32_t width,
    uint32_t height, uint32_t quality, hal_jpegdata *jpeg)
{
    uint32_t q[92] = {0}, mode = FH8626_JPEG_MODE_SNAPSHOT;
    uint32_t len, user;
    uintptr_t base, end;
    int rc;

    if (!k || !jpeg || !width || !height)
        return -EINVAL;
    pthread_mutex_lock(&k->jpeg_lock);
    if (!k->jpeg_slot[0].ready || width > k->jpeg_slot[0].width ||
        height > k->jpeg_slot[0].height ||
        quality != k->jpeg_slot[0].quality) {
        pthread_mutex_unlock(&k->jpeg_lock);
        return -ENOTSUP;
    }
    q[0] = FH8626_JPEG_STREAM_MASK;
    rc = ioctl(k->media_fd, FH8626_MEDIA_STREAM_6, q);
    if (rc < 0) {
        rc = errno ? -errno : -EAGAIN;
        HAL_WARNING("fh8626", "JPEG snapshot query failed: rc=%#x errno=%d\n",
            rc, errno);
        pthread_mutex_unlock(&k->jpeg_lock);
        return rc;
    }
    len = q[7];
    user = q[6];
    HAL_INFO("fh8626", "JPEG snapshot descriptor: %08x %08x %08x %08x %08x %08x %08x %08x\n",
        q[2], q[3], q[4], q[5], q[6], q[7], q[8], q[9]);
    base = (uintptr_t)k->jpeg_slot[0].mem.virt;
    end = base + k->jpeg_slot[0].mem.size;
    if (!len || len > k->jpeg_slot[0].mem.size ||
        (uintptr_t)user < base || (uintptr_t)user > end - len) {
        (void)ioctl(k->jpeg_fd, FH8626_JPEG_RELEASE, &mode);
        pthread_mutex_unlock(&k->jpeg_lock);
        return -EBADMSG;
    }
    jpeg->data = malloc(len);
    if (!jpeg->data) {
        (void)ioctl(k->jpeg_fd, FH8626_JPEG_RELEASE, &mode);
        pthread_mutex_unlock(&k->jpeg_lock);
        return -ENOMEM;
    }
    memcpy(jpeg->data, (const void *)(uintptr_t)user, len);
    jpeg->length = len;
    jpeg->jpegSize = len;
    rc = ioctl(k->jpeg_fd, FH8626_JPEG_RELEASE, &mode);
    pthread_mutex_unlock(&k->jpeg_lock);
    return rc < 0 ? (errno ? -errno : -EIO) : 0;
}

int fh8626_kernel_jpeg_get(struct fh8626_kernel *k, uint32_t width,
    uint32_t height, uint32_t quality, hal_jpegdata *jpeg)
{
    int rc;

    if (!k)
        return -EINVAL;
    pthread_mutex_lock(&k->jpeg_lock);
    if (k->jpeg_slot[1].ready && k->jpeg_latest && k->jpeg_latest_len &&
        width <= k->jpeg_latest_width && height <= k->jpeg_latest_height &&
        quality == k->jpeg_slot[1].quality) {
        jpeg->data = malloc(k->jpeg_latest_len);
        if (!jpeg->data) {
            pthread_mutex_unlock(&k->jpeg_lock);
            return -ENOMEM;
        }
        memcpy(jpeg->data, k->jpeg_latest, k->jpeg_latest_len);
        jpeg->length = k->jpeg_latest_len;
        jpeg->jpegSize = k->jpeg_latest_len;
        pthread_mutex_unlock(&k->jpeg_lock);
        return 0;
    }
    if (k->jpeg_slot[1].ready) {
        int qrc = quality == k->jpeg_slot[1].quality ? -EAGAIN : -ENOTSUP;
        pthread_mutex_unlock(&k->jpeg_lock);
        return qrc;
    }
    pthread_mutex_unlock(&k->jpeg_lock);
    rc = kernel_jpeg_snapshot_get(k, width, height, quality, jpeg);
    return rc;
}

int fh8626_kernel_jpeg_deinit_mode(struct fh8626_kernel *k, uint32_t wanted_mode)
{
    int i, rc = 0, wanted = jpeg_slot_index(wanted_mode);

    if (!k || wanted < 0)
        return -EINVAL;
    if (wanted_mode == FH8626_JPEG_MODE_MJPEG && k->jpeg_thread_started) {
        k->jpeg_running = 0;
        pthread_join(k->jpeg_thread, NULL);
        k->jpeg_thread_started = 0;
    }
    pthread_mutex_lock(&k->jpeg_lock);
    for (i = 0; i < 2; ++i) {
        uint32_t mode, zero = 0u;
        if (i != wanted)
            continue;
        if (!k->jpeg_slot[i].ready)
            continue;
        mode = k->jpeg_slot[i].mode;
        if (mode == FH8626_JPEG_MODE_MJPEG)
            (void)ioctl(k->jpeg_fd, FH8626_JPEG_STOP, &mode);
        if (ioctl(k->jpeg_fd, FH8626_JPEG_MEM_UNINIT,
                  &(uint32_t[6]){mode, k->jpeg_slot[i].mem.phys,
                      k->jpeg_slot[i].mem.virt, k->jpeg_slot[i].mem.size,
                      k->jpeg_slot[i].width, k->jpeg_slot[i].height}) < 0 && !rc)
            rc = errno ? -errno : -EIO;
        (void)zero;
        free_mem(&k->jpeg_slot[i].mem);
        k->jpeg_slot[i].ready = 0;
    }
    if (k->jpeg_ready && !k->jpeg_slot[0].ready && !k->jpeg_slot[1].ready) {
        uint32_t source = 2u;
        (void)ioctl(k->media_fd, FH8626_MEDIA_UNBIND_SRC, &source);
        k->jpeg_ready = 0;
    }
    if (!k->jpeg_slot[0].ready && !k->jpeg_slot[1].ready &&
        k->jpeg_fd >= 0) {
        close(k->jpeg_fd);
        k->jpeg_fd = -1;
    }
    if (!k->jpeg_slot[1].ready) {
        free(k->jpeg_latest);
        k->jpeg_latest = NULL;
        k->jpeg_latest_len = 0;
    }
    pthread_mutex_unlock(&k->jpeg_lock);
    return rc;
}

int fh8626_kernel_jpeg_deinit(struct fh8626_kernel *k)
{
    int first_error = 0;
    int rc;

    if (!k)
        return -EINVAL;
    rc = fh8626_kernel_jpeg_deinit_mode(k, FH8626_JPEG_MODE_MJPEG);
    if (rc && rc != -ENODEV)
        first_error = rc;
    rc = fh8626_kernel_jpeg_deinit_mode(k, FH8626_JPEG_MODE_SNAPSHOT);
    if (rc && rc != -ENODEV && !first_error)
        first_error = rc;
    return first_error;
}

int fh8626_kernel_start(struct fh8626_kernel **out,
    const struct fh8626_native_config *config, fh8626_video_sink sink)
{
    struct fh8626_kernel *k;
    struct fh8626_native_runtime_ops ops;
    int rc;

    if (!out || !config || !sink)
        return -EINVAL;
    k = calloc(1, sizeof(*k));
    if (!k)
        return -ENOMEM;
    k->media_fd = k->isp_fd = k->pae_fd = k->vmm_fd = k->mem_fd = -1;
    k->jpeg_fd = -1;
    k->sink = sink;
    k->config = *config;
    k->lock_fd = -1;
    rc = pthread_mutex_init(&k->jpeg_lock, NULL);
    if (rc) {
        free(k);
        return -rc;
    }
    k->jpeg_lock_ready = 1;
    rc = pthread_mutex_init(&k->control_lock, NULL);
    if (rc) {
        pthread_mutex_destroy(&k->jpeg_lock);
        free(k);
        return -rc;
    }
    k->control_lock_ready = 1;
    if (!k->config.width || !k->config.height || !k->config.fps ||
        !k->config.bitrate_kbps) {
        pthread_mutex_destroy(&k->control_lock);
        pthread_mutex_destroy(&k->jpeg_lock);
        free(k);
        return -EINVAL;
    }
    ops = (struct fh8626_native_runtime_ops){
        kernel_hal_init, kernel_system_init, kernel_pipeline_create,
        kernel_video_create, kernel_stream_start, kernel_stage_stop,
        kernel_video_destroy, kernel_noop, kernel_system_deinit,
        kernel_hal_deinit};
    rc = fh8626_native_runtime_init(&k->runtime, &ops, k);
    if (!rc)
        rc = fh8626_native_runtime_start(&k->runtime);
    if (rc) {
        (void)fh8626_kernel_stop(k);
        return rc;
    }
    *out = k;
    return 0;
}

int fh8626_kernel_stop(struct fh8626_kernel *k)
{
    int first_error = 0;
    int rc;

    if (!k)
        return -EINVAL;
    if (k->jpeg_lock_ready) {
        rc = fh8626_kernel_jpeg_deinit(k);
        if (rc && !first_error)
            first_error = rc;
    }
    /* Stop/join workers before asking the lifecycle machine to tear down.
     * This gives an in-flight adapter pump a chance to release its descriptor
     * lease before the balanced-state gate is evaluated. */
    kernel_quiesce_threads(k);
    if (k->runtime.life.state != FH8626_LIFE_COLD)
        rc = fh8626_native_runtime_stop(&k->runtime);
    else
        rc = kernel_stage_stop(k);
    if (rc == -EBUSY)
        return rc;
    if (rc && !first_error)
        first_error = rc;

    free(k->scratch);
    if (k->mmio && munmap((void *)k->mmio, FH8626_ISP_MMIO_SIZE) < 0 &&
        !first_error)
        first_error = -errno;
    k->mmio = NULL;
    fh_sensor_gc1054_close(&k->sensor);

    /*
     * Normal runtime_stop already ran system_deinit/hal_deinit. On partial
     * startup, call the same idempotent cleanup here for stages that never
     * became owned by the runtime state machine.
     */
    if (k->pae_fd >= 0 || k->isp_fd >= 0 || k->vmm_fd >= 0) {
        rc = kernel_system_deinit(k);
        if (rc && !first_error)
            first_error = rc;
        rc = kernel_hal_deinit(k);
        if (rc && !first_error)
            first_error = rc;
    }
    if (k->lock_fd >= 0) close(k->lock_fd);
    if (k->control_lock_ready)
        pthread_mutex_destroy(&k->control_lock);
    if (k->jpeg_lock_ready)
        pthread_mutex_destroy(&k->jpeg_lock);
    free(k);
    return first_error;
}

int fh8626_kernel_is_running(const struct fh8626_kernel *k)
{
    return k && k->running;
}

int fh8626_kernel_request_idr(struct fh8626_kernel *k)
{
    struct fh_h264_control control;

    if (!k || k->pae_fd < 0 || !k->running)
        return -ENODEV;
    memset(&control, 0, sizeof(control));
    control.ioctl = kernel_h264_ioctl;
    control.opaque = k;
    control.channel = FH8626_NATIVE_CHANNEL;
    return fh_h264_force_i(&control);
}

int fh8626_kernel_set_bitrate(struct fh8626_kernel *k, uint32_t bitrate_kbps)
{
    struct fh_h264_control control;
    struct fh_pae_rc_wire current;
    struct fh_pae_rc_realtime_wire realtime;
    uint32_t rate;
    int rc;

    if (!k || k->pae_fd < 0 || !k->running || !bitrate_kbps)
        return -EINVAL;
    if (bitrate_kbps > UINT32_MAX / 1000u)
        return -ERANGE;

    memset(&control, 0, sizeof(control));
    control.ioctl = kernel_h264_ioctl;
    control.opaque = k;
    control.channel = FH8626_NATIVE_CHANNEL;

    rc = fh_h264_get_rc(&control, &current);
    if (rc)
        return rc;

    if (current.rc_mode != FH_PAE_RC_VBR &&
        current.rc_mode != FH_PAE_RC_AVBR)
        return -EOPNOTSUPP;

    rate = bitrate_kbps * 1000u;
    rc = fh_h264_build_realtime_bitrate(&current, rate, &realtime);
    if (rc)
        return rc;
    rc = fh_h264_change_rc_realtime(&control, &realtime);
    if (!rc)
        k->config.bitrate_kbps = bitrate_kbps;
    return rc;
}

int fh8626_kernel_set_mirror_flip(struct fh8626_kernel *k,
    int mirror, int flip)
{
    uint32_t old_logical, next_logical;
    uint32_t old_bayer, next_bayer;
    int rc;

    if (!k || !k->running || !k->control_lock_ready)
        return -ENODEV;

    old_logical = (k->config.mirror ? 2u : 0u) |
                  (k->config.flip ? 1u : 0u);
    next_logical = (mirror ? 2u : 0u) | (flip ? 1u : 0u);
    if (old_logical == next_logical)
        return 0;

    rc = fh_sensor_gc1054_bayer_for_mirror_flip(old_logical, &old_bayer);
    if (rc)
        return rc;
    rc = fh_sensor_gc1054_bayer_for_mirror_flip(next_logical, &next_bayer);
    if (rc)
        return rc;

    pthread_mutex_lock(&k->control_lock);

    rc = fh_sensor_gc1054_set_mirror_flip(&k->sensor, next_logical);
    if (!rc)
        rc = fh_isp_runtime_set_bayer_selector(&k->isp_runtime, next_bayer);
    if (rc) {
        (void)fh_sensor_gc1054_set_mirror_flip(&k->sensor, old_logical);
        (void)fh_isp_runtime_set_bayer_selector(&k->isp_runtime, old_bayer);
    } else {
        k->config.mirror = mirror ? 1u : 0u;
        k->config.flip = flip ? 1u : 0u;
    }

    pthread_mutex_unlock(&k->control_lock);
    return rc;
}
