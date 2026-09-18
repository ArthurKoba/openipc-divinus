#include "fh8626_hal.h"
#include "native/h264/fh8626_h264_rc.h"
#ifdef FH8626_NATIVE_KERNEL
#include "fh8626_kernel.h"
#endif
#include "../globals.h"
#include "../../app_config.h"

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define FH8626_STUB_RING_BASE 0x10000000u
#define FH8626_STUB_RING_SIZE 4096u
#define FH8626_STUB_FRAME_INTERVAL_US 40000u

hal_chnstate fh8626_state[FH8626_VENC_CHN_NUM];

struct fh8626_osd_request {
    hal_rect rect;
    uint8_t opacity;
    int ready;
};
static struct fh8626_osd_request fh8626_osd_request[FH8626_OSD_HW_SLOTS];

#ifdef FH8626_NATIVE_KERNEL
static struct fh8626_kernel *kernel_context;
#endif

#ifdef FH8626_NATIVE_STUB
struct fh8626_stub_context {
    struct fh8626_native_runtime runtime;
    struct fh8626_stream_backend backend;
    struct fh8626_native_adapter adapter;
    pthread_t thread;
    pthread_mutex_t lock;
    int lock_ready;
    int running;
    fh8626_video_sink sink;
    uint8_t ring[FH8626_STUB_RING_SIZE];
    uint8_t scratch[FH8626_STUB_RING_SIZE];
    uint32_t desc[FH8626_MEDIA_STREAM_DESC_WORDS];
    unsigned media_calls;
    unsigned step_calls;
    unsigned eagain_every;
    int wrap_frame;
};

static struct fh8626_stub_context stub;

static int stage_ok(void *opaque)
{
    (void)opaque;
    return 0;
}

static struct fh8626_native_runtime_ops stub_runtime_ops(void)
{
    struct fh8626_native_runtime_ops ops;

    memset(&ops, 0, sizeof(ops));
    ops.hal_init = stage_ok;
    ops.system_init = stage_ok;
    ops.pipeline_create = stage_ok;
    ops.video_create = stage_ok;
    ops.stream_start = stage_ok;
    ops.stream_stop = stage_ok;
    ops.video_destroy = stage_ok;
    ops.pipeline_destroy = stage_ok;
    ops.system_deinit = stage_ok;
    ops.hal_deinit = stage_ok;
    return ops;
}

static unsigned stub_env_unsigned(const char *name)
{
    const char *value = getenv(name);
    char *end = NULL;
    unsigned long parsed;

    if (!value || !*value)
        return 0;
    errno = 0;
    parsed = strtoul(value, &end, 10);
    if (errno || !end || *end || parsed > UINT_MAX)
        return 0;
    return (unsigned)parsed;
}

static int stub_env_enabled(const char *name)
{
    const char *value = getenv(name);

    return value && (!strcmp(value, "1") || !strcmp(value, "true") ||
        !strcmp(value, "yes") || !strcmp(value, "on"));
}

static void stub_prepare_frame(struct fh8626_stub_context *ctx)
{
    static const uint8_t frame[] = {
        0x00,0x00,0x00,0x01,0x67,0x42,0x00,0x1f,
        0x00,0x00,0x00,0x01,0x68,0xce,0x06,0xe2,
        0x00,0x00,0x00,0x01,0x65,0x88,0x84,0x21
    };
    uint32_t offset = 32u;
    size_t tail;

    if (ctx->wrap_frame)
        offset = FH8626_STUB_RING_SIZE - 10u;

    memset(ctx->ring, 0, sizeof(ctx->ring));
    memset(ctx->desc, 0, sizeof(ctx->desc));
    tail = sizeof(ctx->ring) - offset;
    if (sizeof(frame) <= tail) {
        memcpy(ctx->ring + offset, frame, sizeof(frame));
    } else {
        memcpy(ctx->ring + offset, frame, tail);
        memcpy(ctx->ring, frame + tail, sizeof(frame) - tail);
    }
    ctx->desc[1] = FH8626_MEDIA_STREAM_KIND;
    ctx->desc[6] = 0x22000000u;
    ctx->desc[7] = FH8626_STUB_RING_BASE + offset;
    ctx->desc[8] = (uint32_t)sizeof(frame);
}

static int stub_ioctl(void *opaque, int fd, unsigned long request, void *arg)
{
    struct fh8626_stub_context *ctx = opaque;
    (void)fd;

    if (request == FH8626_MEDIA_STREAM_6) {
        ctx->media_calls++;
        if (ctx->eagain_every && ctx->media_calls % ctx->eagain_every == 0u)
            return -EAGAIN;
        memcpy(arg, ctx->desc, sizeof(ctx->desc));
        return 0;
    }
    if (request == FH8626_PAE_STREAM_STEP) {
        uint32_t *channel = arg;
        if (!channel || *channel != FH8626_NATIVE_CHANNEL)
            return -EINVAL;
        ctx->step_calls++;
        return 0;
    }
    return -ENOTTY;
}

static int stub_copy(void *opaque, uint8_t *dst, const uint8_t *src, size_t len)
{
    struct fh8626_stub_context *ctx = opaque;
    uintptr_t logical = (uintptr_t)src;
    size_t offset;

    if (logical < FH8626_STUB_RING_BASE)
        return -ERANGE;
    offset = (size_t)(logical - FH8626_STUB_RING_BASE);
    if (offset > sizeof(ctx->ring) || len > sizeof(ctx->ring) - offset)
        return -ERANGE;
    memcpy(dst, ctx->ring + offset, len);
    return 0;
}

static int stub_running(struct fh8626_stub_context *ctx)
{
    int running;

    pthread_mutex_lock(&ctx->lock);
    running = ctx->running;
    pthread_mutex_unlock(&ctx->lock);
    return running;
}

static void *fh8626_stub_video_thread(void *opaque)
{
    struct fh8626_stub_context *ctx = opaque;

    while (keepRunning && stub_running(ctx)) {
        int ret = fh8626_native_adapter_pump(&ctx->adapter, ctx->sink);
        if (ret && ret != -EAGAIN)
            break;
        usleep(FH8626_STUB_FRAME_INTERVAL_US);
    }
    return NULL;
}
#endif

int fh8626_hal_stub_enabled(void)
{
    struct fh8626_provider_status status;

    return fh8626_provider_get_status(&status) == 0 &&
        status.kind == FH8626_PROVIDER_STUB && status.selectable;
}

int fh8626_hal_production_ready(void)
{
    return fh8626_provider_production_ready();
}

int fh8626_sdk_start(fh8626_video_sink sink)
{
#ifdef FH8626_NATIVE_KERNEL
    struct fh8626_native_config config;
    hal_vidconfig requested;

    if (kernel_context)
        return -EBUSY;
    memset(fh8626_osd_request, 0, sizeof(fh8626_osd_request));
    if (app_config.mp4_width > UINT16_MAX ||
        app_config.mp4_height > UINT16_MAX ||
        app_config.mp4_fps > UINT8_MAX ||
        app_config.mp4_gop > UINT8_MAX ||
        app_config.mp4_bitrate > UINT16_MAX)
        return -ERANGE;
    memset(&requested, 0, sizeof(requested));
    requested.width = app_config.mp4_width;
    requested.height = app_config.mp4_height;
    requested.codec = app_config.mp4_codecH265 ?
        HAL_VIDCODEC_H265 : HAL_VIDCODEC_H264;
    requested.mode = app_config.mp4_mode;
    requested.profile = app_config.mp4_profile;
    requested.gop = app_config.mp4_gop;
    requested.framerate = app_config.mp4_fps;
    requested.bitrate = app_config.mp4_bitrate;
    requested.minQual = app_config.mp4_iqp;
    requested.maxQual = app_config.mp4_pqp;
    if (!fh8626_video_contract_known(&requested))
        return -ENOTSUP;
    config = (struct fh8626_native_config){
        app_config.mp4_width, app_config.mp4_height, app_config.mp4_fps,
        app_config.mp4_gop,
        app_config.mp4_profile == HAL_VIDPROFILE_MAIN ? 0x4du : 0x42u,
        app_config.mirror ? 1u : 0u, app_config.flip ? 1u : 0u,
        app_config.mp4_mode == HAL_VIDMODE_VBR ? FH_PAE_RC_VBR :
        app_config.mp4_mode == HAL_VIDMODE_QP ? FH_PAE_RC_FIXED_QP :
        app_config.mp4_mode == HAL_VIDMODE_AVBR ? FH_PAE_RC_AVBR :
        app_config.mp4_mode == HAL_VIDMODE_CVBR ? FH_PAE_RC_CVBR :
        FH_PAE_RC_CBR,
        app_config.mp4_bitrate, app_config.mp4_iqp, app_config.mp4_pqp,
        app_config.mp4_secondary_bitrate, app_config.mp4_extra_qp};
    {
        int native_ret = fh8626_kernel_start(&kernel_context, &config, sink);
        if (!native_ret) {
            memset(fh8626_state, 0, sizeof(fh8626_state));
            fh8626_state[0].enable = 1;
            fh8626_state[0].mainLoop = 1;
            fh8626_state[0].payload = HAL_VIDCODEC_H264;
            strcpy(sensor, "GC1054");
        }
        return native_ret;
    }
#endif
#ifndef FH8626_NATIVE_STUB
    (void)sink;
    return -ENOTSUP;
#else
    struct fh8626_native_runtime_ops ops;
    int ret;

    if (!sink)
        return -EINVAL;
    if (stub.lock_ready)
        return -EBUSY;
    memset(&stub, 0, sizeof(stub));
    stub.eagain_every = stub_env_unsigned("FH8626_STUB_EAGAIN_EVERY");
    stub.wrap_frame = stub_env_enabled("FH8626_STUB_WRAP");
    ret = pthread_mutex_init(&stub.lock, NULL);
    if (ret)
        return -ret;
    stub.lock_ready = 1;
    stub.sink = sink;
    stub_prepare_frame(&stub);

    ops = stub_runtime_ops();
    ret = fh8626_native_runtime_init(&stub.runtime, &ops, &stub);
    if (ret)
        goto fail;
    ret = fh8626_native_runtime_start(&stub.runtime);
    if (ret)
        goto fail;
    ret = fh8626_stream_backend_init(&stub.backend, 3, 4,
        FH8626_STUB_RING_BASE, FH8626_STUB_RING_SIZE, stub_ioctl, &stub,
        &stub.runtime.life);
    if (ret)
        goto stop_runtime;
    ret = fh8626_native_adapter_init(&stub.adapter, &stub.backend,
        stub.scratch, sizeof(stub.scratch), stub_copy, &stub,
        FH8626_STUB_FRAME_INTERVAL_US);
    if (ret)
        goto stop_runtime;

    memset(fh8626_state, 0, sizeof(fh8626_state));
    fh8626_state[0].enable = 1;
    fh8626_state[0].mainLoop = 1;
    fh8626_state[0].payload = HAL_VIDCODEC_H264;

    pthread_mutex_lock(&stub.lock);
    stub.running = 1;
    pthread_mutex_unlock(&stub.lock);
    ret = pthread_create(&stub.thread, NULL, fh8626_stub_video_thread, &stub);
    if (ret) {
        pthread_mutex_lock(&stub.lock);
        stub.running = 0;
        pthread_mutex_unlock(&stub.lock);
        ret = -ret;
        goto stop_runtime;
    }
    return 0;

stop_runtime:
    (void)fh8626_native_runtime_stop(&stub.runtime);
fail:
    if (stub.lock_ready) {
        pthread_mutex_destroy(&stub.lock);
        stub.lock_ready = 0;
    }
    return ret;
#endif
}

int fh8626_native_active(void)
{
#ifdef FH8626_NATIVE_KERNEL
    return kernel_context != NULL;
#elif defined(FH8626_NATIVE_STUB)
    return stub.lock_ready && stub.running;
#else
    return 0;
#endif
}

int fh8626_request_idr(void)
{
#ifdef FH8626_NATIVE_KERNEL
    if (!kernel_context)
        return -ENODEV;
    return fh8626_kernel_request_idr(kernel_context);
#else
    return -ENOTSUP;
#endif
}

int fh8626_set_bitrate(uint32_t bitrate_kbps)
{
#ifdef FH8626_NATIVE_KERNEL
    if (!kernel_context)
        return -ENODEV;
    return fh8626_kernel_set_bitrate(kernel_context, bitrate_kbps);
#else
    (void)bitrate_kbps;
    return -ENOTSUP;
#endif
}

int fh8626_set_mirror_flip(int mirror, int flip)
{
#ifdef FH8626_NATIVE_KERNEL
    if (!kernel_context)
        return -ENODEV;
    return fh8626_kernel_set_mirror_flip(kernel_context, mirror, flip);
#else
    (void)mirror;
    (void)flip;
    return -ENOTSUP;
#endif
}

int fh8626_set_grayscale(int enabled)
{
#ifdef FH8626_NATIVE_KERNEL
    if (!kernel_context)
        return -ENODEV;
    return fh8626_kernel_set_grayscale(kernel_context, enabled);
#else
    (void)enabled;
    return -ENOTSUP;
#endif
}

int fh8626_jpeg_init(uint32_t mode, uint32_t width, uint32_t height,
    uint32_t quality, uint32_t fps, uint32_t bitrate, uint32_t rc_mode)
{
#ifdef FH8626_NATIVE_KERNEL
    if (!kernel_context)
        return -ENODEV;
    return fh8626_kernel_jpeg_init(kernel_context, mode, width, height,
        quality, fps, bitrate, rc_mode);
#else
    (void)mode; (void)width; (void)height; (void)quality;
    (void)fps; (void)bitrate; (void)rc_mode;
    return -ENOTSUP;
#endif
}

void fh8626_jpeg_deinit(void)
{
#ifdef FH8626_NATIVE_KERNEL
    if (kernel_context)
        (void)fh8626_kernel_jpeg_deinit(kernel_context);
#endif
}

void fh8626_jpeg_deinit_mode(uint32_t mode)
{
#ifdef FH8626_NATIVE_KERNEL
    if (kernel_context)
        (void)fh8626_kernel_jpeg_deinit_mode(kernel_context, mode);
#else
    (void)mode;
#endif
}

int fh8626_jpeg_get(uint32_t width, uint32_t height, uint32_t quality,
    hal_jpegdata *jpeg)
{
#ifdef FH8626_NATIVE_KERNEL
    if (!kernel_context)
        return -ENODEV;
    return fh8626_kernel_jpeg_get(kernel_context, width, height, quality,
        jpeg);
#else
    (void)width; (void)height; (void)quality; (void)jpeg;
    return -ENOTSUP;
#endif
}

int fh8626_hal_stub_get_stats(struct fh8626_stub_stats *stats)
{
    if (!stats)
        return -EINVAL;
#ifndef FH8626_NATIVE_STUB
    memset(stats, 0, sizeof(*stats));
    return -ENOTSUP;
#else
    stats->media_calls = stub.media_calls;
    stats->step_calls = stub.step_calls;
    stats->frames_delivered = stub.adapter.frames_delivered;
    stats->sink_errors = stub.adapter.sink_errors;
    stats->leases_started = stub.runtime.life.leases_started;
    stats->leases_released = stub.runtime.life.leases_released;
    stats->life_state = stub.runtime.life.state;
    return 0;
#endif
}

int fh8626_sdk_stop(void)
{
#ifdef FH8626_NATIVE_KERNEL
    if (kernel_context) {
        int native_ret = fh8626_kernel_stop(kernel_context);
        /* -EBUSY means the owner context was deliberately retained rather than
         * freeing resources under an outstanding stream lease. */
        if (native_ret == -EBUSY)
            return native_ret;
        kernel_context = NULL;
        memset(fh8626_state, 0, sizeof(fh8626_state));
        return native_ret;
    }
#endif
#ifndef FH8626_NATIVE_STUB
    return -ENOTSUP;
#else
    int ret;

    if (!stub.lock_ready)
        return 0;
    pthread_mutex_lock(&stub.lock);
    stub.running = 0;
    pthread_mutex_unlock(&stub.lock);
    pthread_join(stub.thread, NULL);

    ret = fh8626_native_runtime_stop(&stub.runtime);
    pthread_mutex_destroy(&stub.lock);
    stub.lock_ready = 0;
    memset(fh8626_state, 0, sizeof(fh8626_state));
    return ret;
#endif
}

int fh8626_region_create(unsigned id, hal_rect rect, uint8_t opacity)
{
#ifdef FH8626_NATIVE_KERNEL
    if (id >= FH8626_OSD_HW_SLOTS)
        return -ENOTSUP;
    if (!rect.width || !rect.height)
        return -EINVAL;
    fh8626_osd_request[id].rect = rect;
    fh8626_osd_request[id].opacity = opacity;
    fh8626_osd_request[id].ready = 1;
    return 0;
#else
    (void)id; (void)rect; (void)opacity;
    return -ENOTSUP;
#endif
}

int fh8626_region_setbitmap(unsigned id, const hal_bitmap *bitmap)
{
#ifdef FH8626_NATIVE_KERNEL
    if (!kernel_context)
        return -ENODEV;
    if (id >= FH8626_OSD_HW_SLOTS)
        return -ENOTSUP;
    if (!fh8626_osd_request[id].ready)
        return -EINVAL;
    return fh8626_kernel_osd_set(kernel_context, id,
        &fh8626_osd_request[id].rect, fh8626_osd_request[id].opacity,
        bitmap);
#else
    (void)id; (void)bitmap;
    return -ENOTSUP;
#endif
}

int fh8626_region_destroy(unsigned id)
{
#ifdef FH8626_NATIVE_KERNEL
    int rc;

    if (id >= FH8626_OSD_HW_SLOTS)
        return -ENOTSUP;
    memset(&fh8626_osd_request[id], 0, sizeof(fh8626_osd_request[id]));
    if (!kernel_context)
        return 0;
    rc = fh8626_kernel_osd_destroy(kernel_context, id);
    return rc;
#else
    (void)id;
    return -ENOTSUP;
#endif
}
