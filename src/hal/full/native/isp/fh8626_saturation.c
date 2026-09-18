#include "fh8626_saturation.h"

#include <errno.h>
#include <string.h>

static uint8_t clamp_u8(int value)
{
    if (value < 0)
        return 0u;
    if (value > 255)
        return 255u;
    return (uint8_t)value;
}

void fh8626_saturation_decode_ctx(
    const uint8_t ctx[FH8626_SATURATION_CTX_BYTES],
    struct fh8626_saturation_attr *attr)
{
    int8_t packed;

    if (!ctx || !attr)
        return;

    memset(attr, 0, sizeof(*attr));
    packed = (int8_t)ctx[0];
    attr->gain_mapping_enable = ctx[0] & 1u;
    attr->roll_angle = (int8_t)(packed >> 1);
    attr->value_05 = ctx[1];
    attr->value_06 = ctx[2];
    attr->value_07 = ctx[3];
    memcpy(attr->curve, ctx + 4u, FH8626_SATURATION_CURVE_BYTES);
}

int fh8626_saturation_encode_ctx(
    const struct fh8626_saturation_attr *attr,
    uint8_t ctx[FH8626_SATURATION_CTX_BYTES])
{
    int angle;

    if (!attr || !ctx)
        return -EINVAL;

    angle = attr->roll_angle;
    if (attr->gain_mapping_enable > 1u || angle < -64 || angle > 63)
        return -ERANGE;

    ctx[0] = (uint8_t)((attr->gain_mapping_enable & 1u) |
        (((uint8_t)angle & 0x7fu) << 1));
    ctx[1] = attr->value_05;
    ctx[2] = attr->value_06;
    ctx[3] = attr->value_07;
    memcpy(ctx + 4u, attr->curve, FH8626_SATURATION_CURVE_BYTES);
    return 0;
}

int fh8626_saturation_build_stock_night(
    const uint8_t current_ctx[FH8626_SATURATION_CTX_BYTES],
    const uint8_t baseline[FH8626_SATURATION_CURVE_BYTES],
    uint8_t out_ctx[FH8626_SATURATION_CTX_BYTES])
{
    struct fh8626_saturation_attr attr;
    int delta;
    unsigned i;

    if (!current_ctx || !baseline || !out_ctx)
        return -EINVAL;

    fh8626_saturation_decode_ctx(current_ctx, &attr);
    attr.gain_mapping_enable = 1u;
    delta = 1 - (int)baseline[0];
    for (i = 0; i < FH8626_SATURATION_CURVE_BYTES; ++i)
        attr.curve[i] = clamp_u8((int)baseline[i] + delta);

    return fh8626_saturation_encode_ctx(&attr, out_ctx);
}
