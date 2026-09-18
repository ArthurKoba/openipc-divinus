#define _GNU_SOURCE
#include "fh8626_sensor_gc1054.h"
#include "reimplementation/fh8626_libgc1054_mipi_reimplementation.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

enum {
    CB_NAME      = 0x00,
    CB_SET_GAIN  = 0x04,
    CB_GET_VI    = 0x08,
    CB_GET_GAIN  = 0x0c,
    CB_SET_INTT  = 0x10,
    CB_UPDATE    = 0x14,
    CB_GET_INTT  = 0x18,
    CB_SET_MF    = 0x1c,
    CB_GET_MF    = 0x20,
    CB_INIT      = 0x28,
    CB_CLOSE     = 0x30,
    CB_SET_FMT   = 0x34,
    CB_WRITE_REG = 0x3c,
    CB_MAX_INTT  = 0x40,
    CB_CONTROL   = 0x4c,
    CB_COMMAND   = 0x64,
};

static void *wire_ptr_at(const uint8_t *cb, unsigned off)
{
    uint32_t wire = 0u;

    if (!cb || off + sizeof(wire) > FH_SENSOR_CB_SIZE)
        return NULL;
    memcpy(&wire, cb + off, sizeof(wire));
    return (void *)(uintptr_t)wire;
}

void *fh_sensor_gc1054_cb(const struct fh_sensor_gc1054 *s, unsigned off)
{
    return s ? wire_ptr_at(s->cb, off) : NULL;
}

const char *fh_sensor_gc1054_name(const struct fh_sensor_gc1054 *s)
{
    return s && s->cb ? "gc1054_mipi" : NULL;
}

int fh_sensor_gc1054_open(struct fh_sensor_gc1054 *s)
{
    if (!s)
        return -EINVAL;
    memset(s, 0, sizeof(*s));
#if defined(__arm__)
    _Static_assert(sizeof(void *) == 4u,
        "FH8626 stock callback ABI requires 32-bit pointers");
#endif
    s->cb = (uint8_t *)Sensor_Create();
    if (!s->cb)
        return -EIO;
    if (!fh_sensor_gc1054_cb(s, CB_INIT) ||
        !fh_sensor_gc1054_cb(s, CB_SET_FMT) ||
        !fh_sensor_gc1054_cb(s, CB_SET_GAIN) ||
        !fh_sensor_gc1054_cb(s, CB_SET_INTT))
        return -EINVAL;
    return 0;
}

void fh_sensor_gc1054_close(struct fh_sensor_gc1054 *s)
{
    if (!s)
        return;
    if (s->cb)
        (void)fh8626_gc1054_source_close();
    memset(s, 0, sizeof(*s));
}

int fh_sensor_gc1054_init(struct fh_sensor_gc1054 *s)
{
    int rc;

    if (!s || !s->cb)
        return -EINVAL;
    rc = fh8626_gc1054_source_initialize_strict();
    if (!rc)
        s->initialized = 1;
    return rc;
}

int fh_sensor_gc1054_set_fmt(struct fh_sensor_gc1054 *s, uint32_t fmt)
{
    if (!s || !s->initialized)
        return -ENODEV;
    return fh8626_gc1054_source_set_format(fmt);
}

int fh_sensor_gc1054_set_intt(struct fh_sensor_gc1054 *s, uint32_t intt)
{
    if (!s || !s->initialized)
        return -ENODEV;
    return fh8626_gc1054_source_set_integration(intt);
}

int fh_sensor_gc1054_set_gain(struct fh_sensor_gc1054 *s, uint32_t gain)
{
    if (!s || !s->initialized)
        return -ENODEV;
    return fh8626_gc1054_source_set_gain(gain);
}

int fh_sensor_gc1054_get_gain(struct fh_sensor_gc1054 *s, uint32_t *gain)
{
    if (!s || !s->initialized || !gain)
        return -EINVAL;
    return fh8626_gc1054_source_get_gain(gain);
}

int fh_sensor_gc1054_get_intt(struct fh_sensor_gc1054 *s, uint32_t *intt)
{
    if (!s || !s->initialized || !intt)
        return -EINVAL;
    return fh8626_gc1054_source_get_integration(intt);
}

int fh_sensor_gc1054_set_vts_multiplier(struct fh_sensor_gc1054 *s,
    uint32_t multiplier)
{
    if (!s || !s->initialized)
        return -ENODEV;
    return fh8626_gc1054_source_set_vts_multiplier(multiplier);
}

int fh_sensor_gc1054_get_vi_attr(struct fh_sensor_gc1054 *s, void *attr)
{
    if (!s || !s->initialized || !attr)
        return -EINVAL;
    return fh8626_gc1054_source_get_vi_attr(attr);
}

int fh_sensor_gc1054_write_reg(struct fh_sensor_gc1054 *s, uint32_t reg,
    uint32_t value)
{
    if (!s || !s->initialized)
        return -ENODEV;
    Sensor_Write(reg, value);
    return 0;
}

int fh_sensor_gc1054_read_reg(struct fh_sensor_gc1054 *s, uint32_t reg,
    uint32_t *value)
{
    uint32_t v;

    if (!s || !s->initialized || !value)
        return -EINVAL;
    v = Sensor_Read(reg);
    if (v == 0xffffu)
        return -EIO;
    *value = v;
    return 0;
}

int fh_sensor_gc1054_set_mirror_flip(struct fh_sensor_gc1054 *s,
    uint32_t logical)
{
    if (!s || !s->initialized)
        return -ENODEV;
    return fh8626_gc1054_source_set_mirror_flip(logical & 3u);
}

int fh_sensor_gc1054_get_mirror_flip(struct fh_sensor_gc1054 *s,
    uint32_t *logical)
{
    if (!s || !s->initialized || !logical)
        return -EINVAL;
    return fh8626_gc1054_source_get_mirror_flip(logical);
}

int fh_sensor_gc1054_bayer_for_mirror_flip(uint32_t logical, uint32_t *bayer)
{
    static const uint8_t normal_map[4] = {0u, 3u, 1u, 2u};

    if (!bayer || logical > 3u)
        return -EINVAL;
    *bayer = normal_map[logical];
    return 0;
}

void fh_sensor_gc1054_awb_gain(void *opaque, uint32_t gain[3])
{
    (void)opaque;
    (void)gain;
}

void fh_sensor_gc1054_awb_query(void *opaque, uint32_t gain[3])
{
    (void)opaque;
    (void)gain;
}

void fh_sensor_gc1054_dump(const struct fh_sensor_gc1054 *s)
{
    static const struct { unsigned off; const char *name; } e[] = {
        {CB_NAME,"name"},{CB_SET_GAIN,"set_gain"},{CB_GET_VI,"get_vi_attr"},
        {CB_GET_GAIN,"get_gain"},{CB_SET_INTT,"set_intt"},{CB_UPDATE,"update"},
        {CB_GET_INTT,"get_intt"},{CB_SET_MF,"set_mirror_flip"},
        {CB_GET_MF,"get_mirror_flip"},{CB_INIT,"init"},{CB_CLOSE,"close"},
        {CB_SET_FMT,"set_fmt"},{CB_WRITE_REG,"write_reg"},
        {CB_MAX_INTT,"max_intt_diff"},{CB_CONTROL,"control"},
        {CB_COMMAND,"command"},
    };
    size_t i;

    printf("SENSOR name=%s cb=%p initialized=%d\n",
        fh_sensor_gc1054_name(s) ?: "(null)",
        s ? (void *)s->cb : NULL, s ? s->initialized : 0);
    for (i = 0; i < sizeof(e) / sizeof(e[0]); ++i)
        printf("  +%02x %-16s %p\n", e[i].off, e[i].name,
            fh_sensor_gc1054_cb(s, e[i].off));
}
