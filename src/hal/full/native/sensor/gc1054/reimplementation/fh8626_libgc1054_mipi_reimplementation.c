#define _GNU_SOURCE
#include "fh8626_libgc1054_mipi_reimplementation.h"
#include "../gc1054_native_contract.h"
#include "fh8626_libmipi_reimplementation.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define FH_I2C_M_RD        0x0001u
#define FH_I2C_MULTI_FLAGS 0x8000u
#define FH_SENSOR_CB_SLOTS 26u
#define FH_SENSOR_CB_SIZE  0x68u

/* Exact 32-bit I2C message ABI consumed by ioctl 0x707. */
struct fh_i2c_msg32 {
    uint16_t addr;
    uint16_t flags;
    uint16_t len;
    uint16_t pad;
    uint32_t buf;
};

struct fh_i2c_rdwr32 {
    uint32_t msgs;
    uint32_t nmsgs;
};

struct fh_clk_rate_request32 {
    uint32_t name;
    uint32_t hz;
};

#if UINTPTR_MAX == 0xffffffffu
_Static_assert(sizeof(struct fh_i2c_msg32) == 12u, "FH I2C message size");
_Static_assert(sizeof(struct fh_i2c_rdwr32) == 8u, "FH I2C request size");
_Static_assert(sizeof(struct fh_clk_rate_request32) == 8u, "FH clock request size");
#endif

/*
 * Exact initialized .data behavior:
 *   sensor fd             = -1
 *   cached gain           = 0x40
 *   cached integration    = 0x6f0
 *   oriented Bayer cache  = {-1,-1,-1,-1}
 * Everything below that belonged to stock .bss and starts at zero.
 */
static const char *sensor_device_path;
static int sensor_fd = -1;
static uint8_t sensor_mode;
static uint16_t sensor_message_addr;
static uint32_t sensor_gain = 0x40u;
static uint32_t sensor_integration = 0x06f0u;
static int32_t oriented_bayer_cache[4] = {-1, -1, -1, -1};

static uint32_t sensor_format;
static int sensor_orientation;
static int sensor_initialized;
static uint32_t sensor_frame_length;
static void (*fps_change_callback)(int32_t old_fps_x10000,
                                   int32_t new_fps_x10000);
static int (*fps_adjust_callback)(int32_t nominal_fps_x10000,
                                 int32_t *target_fps_x10000);
static uint32_t sensor_frame_override;

/* Exact target callback object: 26 x 32-bit slots = 0x68 bytes. */
static uint32_t sensor_callbacks[FH_SENSOR_CB_SLOTS];

static uint32_t ptr32(const void *p)
{
    return (uint32_t)(uintptr_t)p;
}

static uint32_t fn32(void (*p)(void))
{
    return (uint32_t)(uintptr_t)p;
}

static int32_t stock_double_to_i32(double value)
{
    if (value != value)
        return 0;
    if (value >= (double)INT32_MAX)
        return INT32_MAX;
    if (value <= (double)INT32_MIN)
        return INT32_MIN;
    return (int32_t)value;
}

static uint32_t stock_double_to_u32(double value)
{
    if (value != value || value <= 0.0)
        return 0u;
    if (value >= (double)UINT32_MAX)
        return UINT32_MAX;
    return (uint32_t)value;
}

static int sensor_ioctl_rdwr(struct fh_i2c_msg32 *msgs, uint32_t nmsgs)
{
    struct fh_i2c_rdwr32 request;

    request.msgs = (uint32_t)(uintptr_t)msgs;
    request.nmsgs = nmsgs;
    return ioctl(sensor_fd, FH8626_GC1054_I2C_IOCTL_RDWR, &request);
}

static unsigned build_write_bytes(uint32_t reg, uint32_t value, uint8_t data[4])
{
    switch (sensor_mode) {
    case 0:
        data[0] = (uint8_t)reg;
        data[1] = (uint8_t)value;
        return 2u;
    case 1:
        data[0] = (uint8_t)reg;
        data[1] = (uint8_t)(value >> 8);
        data[2] = (uint8_t)value;
        return 3u;
    case 2:
        data[0] = (uint8_t)(reg >> 8);
        data[1] = (uint8_t)reg;
        data[2] = (uint8_t)value;
        return 3u;
    default:
        data[0] = (uint8_t)(reg >> 8);
        data[1] = (uint8_t)reg;
        data[2] = (uint8_t)(value >> 8);
        data[3] = (uint8_t)value;
        return 4u;
    }
}

static unsigned build_read_address(uint32_t reg, uint8_t data[2])
{
    if (sensor_mode < 2u) {
        data[0] = (uint8_t)reg;
        return 1u;
    }
    data[0] = (uint8_t)(reg >> 8);
    data[1] = (uint8_t)reg;
    return 2u;
}

static unsigned read_value_size(void)
{
    /*
     * Stock branches explicitly: mode 0 -> 1 byte, mode 1 -> 2,
     * mode 2 -> 1, every other mode -> 2. Do not collapse this to
     * parity: raw I2CSensor_Read remains callable for mode >= 4.
     */
    if (sensor_mode == 0u || sensor_mode == 2u)
        return 1u;
    return 2u;
}

static void i2c_write_common(uint32_t reg, uint32_t value, int quiet,
                             int guard_sensor_mode)
{
    struct fh_i2c_msg32 msg;
    uint8_t data[4];
    int rc;

    if (guard_sensor_mode && sensor_mode > 3u)
        return;

    memset(&msg, 0, sizeof(msg));
    msg.addr = sensor_message_addr;
    msg.len = (uint16_t)build_write_bytes(reg, value, data);
    msg.buf = (uint32_t)(uintptr_t)data;

    rc = sensor_ioctl_rdwr(&msg, 1u);
    if (rc < 0 && !quiet) {
        printf("ERROR: Unable to write sensor register!");
        printf("addr: 0x%x, data: 0x%x\n", reg, value);
    }
}

static uint32_t i2c_read_common(uint32_t reg, int guard_sensor_mode)
{
    struct fh_i2c_msg32 msg[2];
    uint8_t address[2];
    uint32_t data = 0u;
    uint32_t result;
    int rc;

    if (guard_sensor_mode && sensor_mode > 3u)
        return 0u;

    memset(msg, 0, sizeof(msg));
    msg[0].addr = sensor_message_addr;
    msg[0].len = (uint16_t)build_read_address(reg, address);
    msg[0].buf = (uint32_t)(uintptr_t)address;

    msg[1].addr = sensor_message_addr;
    msg[1].flags = FH_I2C_M_RD;
    msg[1].len = (uint16_t)read_value_size();
    msg[1].buf = (uint32_t)(uintptr_t)&data;

    rc = sensor_ioctl_rdwr(msg, 2u);
    if (rc < 0) {
        printf("ERROR: Unable to read sensor register!");
        return 0xffffu;
    }

    if (sensor_mode & 1u) {
        result = ((data >> 8) & 0xffu) | ((data & 0xffu) << 8);
        return result;
    }
    return data & 0xffu;
}

static void i2c_write_multi_common(const uint16_t *regs,
                                   const uint16_t *values,
                                   int quiet, int count,
                                   int guard_sensor_mode)
{
    struct fh_i2c_msg32 *msgs;
    uint8_t *bytes;
    int i, rc;

    if (guard_sensor_mode && sensor_mode > 3u)
        return;

    msgs = malloc((size_t)count * sizeof(*msgs));
    if (!msgs)
        return;
    bytes = malloc((size_t)count * 4u);
    if (!bytes) {
        free(msgs);
        return;
    }

    memset(msgs, 0, (size_t)count * sizeof(*msgs));
    for (i = 0; i < count; ++i) {
        uint8_t *data = bytes + (size_t)i * 4u;
        msgs[i].addr = sensor_message_addr;
        msgs[i].flags = FH_I2C_MULTI_FLAGS;
        msgs[i].len = (uint16_t)build_write_bytes(regs[i], values[i], data);
        msgs[i].buf = (uint32_t)(uintptr_t)data;
    }

    rc = sensor_ioctl_rdwr(msgs, (uint32_t)count);
    free(msgs);
    free(bytes);

    if (rc < 0 && !quiet) {
        printf("ERROR: Unable to write sensor register!");
        printf("addr: 0x%x, data: 0x%x\n",
               (unsigned)regs[0], (unsigned)values[0]);
    }
}

int set_clk_rate(int type, uint32_t hz)
{
    const char *name;
    struct fh_clk_rate_request32 request;
    int fd, rc;

    if (type == 0)
        name = "isp_aclk";
    else if (type == 1)
        name = "cis_clk_out";
    else {
        printf("unknown clock type: %d\n", type);
        return -1;
    }

    request.name = ptr32(name);
    request.hz = hz;
    fd = open("/dev/fh_clk_miscdev", 0x1002);
    if (fd < 0) {
        puts("fh_clk open error");
        return -1;
    }

    rc = ioctl(fd, 0xc0046302ul, &request);
    close(fd);
    if (rc >= 0) {
        printf("set %s freq: %lu hz\n", name, (unsigned long)hz);
        return 0;
    }
    printf("SET_CLK_RATE error, ret=%d\n", rc);
    return -1;
}

void SPISensor_Write(uint32_t reg, uint32_t value)
{
    (void)reg;
    (void)value;
}

uint32_t SPISensor_Read(uint32_t reg)
{
    (void)reg;
    return 0u;
}

void I2CSensor_WriteEx(uint32_t reg, uint32_t value, int quiet)
{
    i2c_write_common(reg, value, quiet != 0, 0);
}

void I2CSensor_WriteEx_Multi(const uint16_t *regs, const uint16_t *values,
                             int quiet, int count)
{
    i2c_write_multi_common(regs, values, quiet != 0, count, 0);
}

void I2CSensor_Write_Multi(const uint16_t *regs, const uint16_t *values,
                           int count)
{
    i2c_write_multi_common(regs, values, 0, count, 0);
}

void I2CSensor_Write(uint32_t reg, uint32_t value)
{
    i2c_write_common(reg, value, 0, 0);
}

uint32_t I2CSensor_Read(uint32_t reg)
{
    return i2c_read_common(reg, 0);
}

uint32_t Sensor_Read(uint32_t reg)
{
    return i2c_read_common(reg, 1);
}

void Sensor_Write(uint32_t reg, uint32_t value)
{
    i2c_write_common(reg, value, 0, 1);
}

void Sensor_Write_Multi(const uint16_t *regs, const uint16_t *values,
                        int count)
{
    i2c_write_multi_common(regs, values, 0, count, 1);
}

void Sensor_WriteEx(uint32_t reg, uint32_t value, int quiet)
{
    i2c_write_common(reg, value, quiet != 0, 1);
}

int SensorDevice_Init(uint16_t message_addr, uint32_t mode)
{
    int rc;

    if (sensor_fd >= 0) {
        close(sensor_fd);
        sensor_fd = -1;
    }

    if (mode < 4u)
        sensor_device_path = "/dev/i2c-0";
    else
        sensor_device_path = "/dev/spi1";

    sensor_mode = (uint8_t)mode;
    sensor_message_addr = message_addr;
    sensor_fd = open(sensor_device_path, 0x802);
    if (sensor_fd < 0)
        printf("ERROR: Unable to open sensor!");

    rc = ioctl(sensor_fd, FH8626_GC1054_I2C_IOCTL_TENBIT, 0ul);
    if (rc < 0) {
        puts("Error: Unable to set address mode!");
        return -1;
    }

    rc = ioctl(sensor_fd, FH8626_GC1054_I2C_IOCTL_FORCE,
               (unsigned long)FH8626_GC1054_I2C_FORCE_ARG);
    if (rc < 0) {
        puts("Error: Unable to set slave address!");
        return -1;
    }
    return 0;
}

int SensorDevice_Close(void)
{
    if (sensor_fd >= 0) {
        close(sensor_fd);
        sensor_fd = -1;
        return 0;
    }
    puts("Error: Sensor device not open !");
    return -1;
}

long SensorGetEnvInt(const char *name, long default_value)
{
    char *value = getenv(name);
    int base;

    if (!value)
        return default_value;

    /*
     * Preserve the stock test literally. It looks like an intended "0x"
     * detector, but ARM code checks NUL in byte 0 before testing X/x in byte 1.
     */
    if (value[0] == '\0' && (((unsigned char)value[1] & 0xdfu) == 0x58u))
        base = 16;
    else
        base = 10;
    return strtol(value, NULL, base);
}

static int Gc1054BuildViAttr(void *attr, double *fps)
{
    const struct fh8626_gc1054_format_contract *f;

    /*
     * Stock clears all 24 output bytes before format dispatch. Unsupported
     * formats therefore return -1 with a zeroed attr structure.
     */
    if (attr)
        memset(attr, 0, sizeof(struct fh8626_gc1054_vi_attr_raw));

    f = fh8626_gc1054_find_format(sensor_format);
    if (!f) {
        printf("[gc1054_mipi]sensor_get_vi_attr: unsupported format %d(0x%08x)\n",
               (int)sensor_format, sensor_format);
        return -1;
    }

    if (attr) {
        struct fh8626_gc1054_vi_attr_raw raw =
            fh8626_gc1054_build_vi_attr(f, sensor_orientation != 0);
        memcpy(attr, &raw, sizeof(raw));
    }
    if (fps)
        *fps = f->nominal_fps;
    return 0;
}

static int Gc1054GetViAttr(void *attr)
{
    if (!attr)
        return -3002;
    return Gc1054BuildViAttr(attr, NULL);
}

static int Gc1054GetGain(uint32_t *gain)
{
    *gain = sensor_gain;
    return 0;
}

static int Gc1054GetIntt(uint32_t *integration)
{
    *integration = sensor_integration;
    return 0;
}

static int Gc1054QueryMaxIntegrationDelta(uint32_t *value)
{
    *value = 5u;
    return 0;
}

static int Gc1054SetIntegration(uint32_t integration)
{
    sensor_integration = integration;
    Sensor_Write(0x03u, (integration >> 8) & 0xffu);
    Sensor_Write(0x04u, integration & 0xffu);
    return 0;
}

static int Gc1054SetGain(uint32_t gain)
{
    struct fh8626_gc1054_gain_program p = fh8626_gc1054_gain_program(gain);

    sensor_gain = gain;
    Sensor_Write(0xfeu, 1u);
    if (p.write_triplet) {
        Sensor_Write(0xb6u, p.b6);
        Sensor_Write(0xb1u, p.b1);
        Sensor_Write(0xb2u, p.b2);
    }
    Sensor_Write(0xfeu, 4u);
    Sensor_Write(0x40u, p.page4_40);
    Sensor_Write(0xfeu, 0u);
    return 0;
}

static int Gc1054ReadMirrorFlipRegister(uint32_t *logical, int orientation)
{
    uint32_t reg, o;

    *logical = 0u;
    Sensor_Write(0xfeu, 0u);
    reg = Sensor_Read(0x17u);
    o = orientation != 0;

    if (o != ((reg >> 1) & 1u))
        *logical |= 1u;
    if (o != (reg & 1u))
        *logical |= 2u;
    return 0;
}

static int Gc1054WriteMirrorFlipRegister(uint32_t logical, int orientation)
{
    uint32_t reg, o, low;

    Sensor_Write(0xfeu, 0u);
    reg = Sensor_Read(0x17u);
    o = orientation != 0;

    low = ((o ^ ((logical >> 1) & 1u)) << 0) |
          ((o ^ (logical & 1u)) << 1);
    reg = (reg & 0xfcu) | low;
    Sensor_Write(0x17u, reg);
    return 0;
}

static int Gc1054SetMirrorFlip(uint32_t logical)
{
    return Gc1054WriteMirrorFlipRegister(logical, sensor_orientation);
}

static int Gc1054GetMirrorFlip(uint32_t *logical)
{
    return Gc1054ReadMirrorFlipRegister(logical, sensor_orientation);
}

static void Gc1054SetFrameLength(uint32_t frame_length)
{
    uint32_t vblank;

    sensor_frame_length = frame_length;

    /*
     * ARM code reads VI word16 at +4, i.e. active height 720, and performs
     * unsigned: frame_length - 16 - 720.
     */
    vblank = frame_length - 736u;
    Sensor_Write(0x07u, (vblank >> 8) & 0xffu);
    Sensor_Write(0x08u, vblank & 0xffu);
}

static int32_t Gc1054GetFrameRateProduct(void)
{
    struct fh8626_gc1054_vi_attr_raw raw;
    double fps;

    if (Gc1054BuildViAttr(&raw, &fps) != 0)
        return 0;
    return stock_double_to_i32((double)raw.word16[0] * fps);
}

static int Gc1054SetVtsMultiplier(uint32_t multiplier)
{
    struct fh8626_gc1054_vi_attr_raw raw;
    uint32_t frame_length;

    if (sensor_frame_override != 0u)
        return 0;

    if (Gc1054BuildViAttr(&raw, NULL) == 0)
        frame_length = multiplier * (uint32_t)raw.word16[0];
    else
        frame_length = 0u;

    if (sensor_frame_length != frame_length)
        Gc1054SetFrameLength(frame_length);
    return 0;
}

static int Gc1054WriteRegCb(uint32_t reg, uint32_t value)
{
    Sensor_Write(reg, value);
    return 0;
}

static int Gc1054Initialize(void)
{
    int words[6] = {5, 0, 0, 0, 0, 1};

    /*
     * Stock ignores both return paths and still marks the callback initialized.
     */
    mipi_init(words);
    (void)SensorDevice_Init(0x21u, 0u);
    sensor_initialized = 1;
    sensor_gain = 0x40u;
    sensor_integration = 0xd0u;
    return 0;
}

static int Gc1054SetFormat(uint32_t format)
{
    const struct fh8626_gc1054_format_contract *f;
    uint32_t old_frame, new_frame;
    int32_t product;
    size_t i;

    if (!sensor_initialized)
        return 0;

    sensor_format = format;
    f = fh8626_gc1054_find_format(format);
    if (!f) {
        printf("[gc1054_mipi]sensor_set_sns_fmt: unsupported format %d(0x%08x)\n",
               (int)format, format);
        return -1;
    }

    for (i = 0; i < FH8626_GC1054_720P25_INIT_COUNT; ++i) {
        struct fh8626_gc1054_reg_write p =
            fh8626_gc1054_format_pair(f, i);
        if (p.reg == 0xffffu)
            usleep(p.value);
        else
            Sensor_Write(p.reg, p.value);
    }

    if (sensor_orientation != 0)
        (void)Gc1054WriteMirrorFlipRegister(3u, 0);

    old_frame = sensor_frame_length;
    new_frame = f->frame_length;
    product = Gc1054GetFrameRateProduct();

    if (fps_change_callback && old_frame != new_frame && product != 0) {
        double old_fps = ((double)product / (double)old_frame) * 10000.0;
        double new_fps = ((double)product / (double)new_frame) * 10000.0;
        int32_t old_x10000 = stock_double_to_i32(old_fps);
        int32_t new_x10000 = stock_double_to_i32(new_fps);
        fps_change_callback(old_x10000, new_x10000);
        printf("[gc1054_mipi lib] notice change fpsx10000 from %d to %d\n",
               old_x10000, new_x10000);
    }

    sensor_frame_override = 0u;
    sensor_frame_length = new_frame;
    return 0;
}

static int Gc1054ControlQuery(const char *name, uint32_t *value)
{
    double fps;
    int32_t product;

    if (strcmp(name, "RGBX") == 0)
        goto unsupported;

    if (strcmp(name, "STD_FRAME_RATE") == 0) {
        if (Gc1054BuildViAttr(NULL, &fps) == 0 && !(fps < 0.0)) {
            *value = stock_double_to_u32(fps * 10000.0);
            return 0;
        }
        goto unsupported;
    }

    if (strcmp(name, "CUR_FRAME_RATE") == 0) {
        product = Gc1054GetFrameRateProduct();
        if (product != 0) {
            *value = stock_double_to_u32(
                ((double)(uint32_t)product /
                 (double)sensor_frame_length) * 10000.0);
            return 0;
        }
        goto unsupported;
    }

    if (strcmp(name, "REAL_FLIP_MIRROR") == 0)
        return Gc1054ReadMirrorFlipRegister(value, 0);

    if (strcmp(name, "MAX_INTT_DIFF") == 0) {
        *value = 5u;
        return 0;
    }

unsupported:
    *value = 0u;
    return -1;
}

static uint32_t Gc1054Command(uint32_t command, int *arg)
{
    const struct fh8626_gc1054_format_contract *f;
    double nominal_fps;
    uint32_t base_frame;
    int32_t nominal_x10000;
    int32_t target_x10000;
    uint32_t frame;

    if (command == 0x80001u) {
        fps_adjust_callback =
            (int (*)(int32_t, int32_t *))(uintptr_t)arg;
        return 0u;
    }

    if (command < 0x80001u) {
        if (command == 1u) {
            *(int16_t *)arg = (int16_t)sensor_frame_length;
            return sensor_frame_length;
        }
        if (command == 0x80000u) {
            fps_change_callback =
                (void (*)(int32_t, int32_t))(uintptr_t)arg;
            return 0u;
        }
        return UINT32_MAX;
    }

    if (command == 0x80003u) {
        sensor_orientation = *arg;
        return 0u;
    }

    if (command != 0x80002u)
        return UINT32_MAX;

    f = fh8626_gc1054_find_format(sensor_format);
    base_frame = f ? f->frame_length : 0u;

    if (*arg == 0) {
        sensor_frame_override = 0u;
        if (sensor_frame_length != base_frame)
            Gc1054SetFrameLength(base_frame);
        return 0u;
    }

    if (!fps_adjust_callback)
        return UINT32_MAX;

    /*
     * Stock still invokes the adjustment callback for an unsupported format.
     * In that case BuildViAttr fails, nominal fps is -10000 and base_frame is
     * zero. Do not short-circuit merely because f is NULL.
     */
    if (f) {
        nominal_fps = f->nominal_fps;
        nominal_x10000 = stock_double_to_i32(nominal_fps * 10000.0);
    } else {
        nominal_x10000 = -10000;
    }

    if (fps_adjust_callback(nominal_x10000, &target_x10000) != 0)
        return UINT32_MAX;

    frame = (uint32_t)stock_double_to_i32(
        ((double)nominal_x10000 / (double)target_x10000) *
        (double)base_frame);
    sensor_frame_override = frame;
    if (sensor_frame_length != frame)
        Gc1054SetFrameLength(frame);
    return 0u;
}

#if defined(__arm__)
__attribute__((naked)) static void Gc1054PassthroughCallback(void)
{
    __asm__ volatile("bx lr");
}
#else
static void Gc1054PassthroughCallback(void)
{
}
#endif

uintptr_t GetDefaultParam(void)
{
    return 0u;
}

uintptr_t GetContrast(void)
{
    return 0u;
}

uintptr_t GetSaturation(void)
{
    return 0u;
}

uintptr_t GetSharpness(void)
{
    return 0u;
}

uint32_t *GetMirrorFlipBayerFormat(void)
{
    if (sensor_orientation == 0)
        return (uint32_t *)(uintptr_t)fh8626_gc1054_bayer_map_normal;

    if (oriented_bayer_cache[0] != -1)
        return (uint32_t *)(uintptr_t)oriented_bayer_cache;

    oriented_bayer_cache[0] = 2;
    oriented_bayer_cache[1] = 1;
    oriented_bayer_cache[2] = 3;
    oriented_bayer_cache[3] = 0;
    return (uint32_t *)(uintptr_t)oriented_bayer_cache;
}

uintptr_t GetSensorAwbGain(void)
{
    return 0u;
}

uintptr_t GetSensorLtmCurve(void)
{
    return 0u;
}

void *Sensor_Create(void)
{
#define CB_FN(index_, fn_)     sensor_callbacks[(index_)] = fn32((void (*)(void))(fn_))

    memset(sensor_callbacks, 0, sizeof(sensor_callbacks));
    sensor_callbacks[0] = ptr32("gc1054_mipi");
    CB_FN(1, Gc1054SetGain);
    CB_FN(2, Gc1054GetViAttr);
    CB_FN(3, Gc1054GetGain);
    CB_FN(4, Gc1054SetIntegration);
    CB_FN(5, Gc1054SetVtsMultiplier);
    CB_FN(6, Gc1054GetIntt);
    CB_FN(7, Gc1054SetMirrorFlip);
    CB_FN(8, Gc1054GetMirrorFlip);
    /* slot 9 / +0x24 = NULL */
    CB_FN(10, Gc1054Initialize);
    CB_FN(11, Gc1054PassthroughCallback);
    CB_FN(12, SensorDevice_Close);
    CB_FN(13, Gc1054SetFormat);
    /* slot 14 / +0x38 = NULL */
    CB_FN(15, Gc1054WriteRegCb);
    CB_FN(16, Gc1054QueryMaxIntegrationDelta);
    /* slots 17,18 / +0x44,+0x48 = NULL */
    CB_FN(19, Gc1054ControlQuery);
    /* slots 20..24 / +0x50..+0x60 = NULL */
    CB_FN(25, Gc1054Command);

#undef CB_FN
    return sensor_callbacks;
}

void Sensor_Destory(void)
{
    /*
     * Stock spelling and behavior: only the 0x68 callback object is cleared.
     * It does NOT close the sensor fd and does NOT undo MIPI mappings.
     */
    memset(sensor_callbacks, 0, sizeof(sensor_callbacks));
}

/* ---- Divinus strict typed adapters ------------------------------------- */

int fh8626_gc1054_source_initialize_strict(void)
{
    int words[6] = {5, 0, 0, 0, 0, 1};
    int rc;

    mipi_init(words);
    rc = SensorDevice_Init(0x21u, 0u);
    if (rc)
        return rc;

    sensor_initialized = 1;
    sensor_gain = 0x40u;
    sensor_integration = 0xd0u;
    return 0;
}

int fh8626_gc1054_source_close(void)
{
    int rc = 0;

    if (sensor_fd >= 0)
        rc = SensorDevice_Close();
    Sensor_Destory();
    return rc;
}

int fh8626_gc1054_source_set_format(uint32_t format)
{
    return Gc1054SetFormat(format);
}

int fh8626_gc1054_source_set_integration(uint32_t integration)
{
    return Gc1054SetIntegration(integration);
}

int fh8626_gc1054_source_set_gain(uint32_t gain)
{
    return Gc1054SetGain(gain);
}

int fh8626_gc1054_source_get_gain(uint32_t *gain)
{
    if (!gain)
        return -EINVAL;
    return Gc1054GetGain(gain);
}

int fh8626_gc1054_source_get_integration(uint32_t *integration)
{
    if (!integration)
        return -EINVAL;
    return Gc1054GetIntt(integration);
}

int fh8626_gc1054_source_set_vts_multiplier(uint32_t multiplier)
{
    return Gc1054SetVtsMultiplier(multiplier);
}

int fh8626_gc1054_source_get_vi_attr(void *attr)
{
    return Gc1054GetViAttr(attr);
}

int fh8626_gc1054_source_set_mirror_flip(uint32_t logical)
{
    return Gc1054SetMirrorFlip(logical);
}

int fh8626_gc1054_source_get_mirror_flip(uint32_t *logical)
{
    if (!logical)
        return -EINVAL;
    return Gc1054GetMirrorFlip(logical);
}
