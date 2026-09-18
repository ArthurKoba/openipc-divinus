#include "fh8626_saturation.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>

static const uint8_t day_ctx[FH8626_SATURATION_CTX_BYTES] = {
    0x01, 0x00, 0x34, 0x30,
    0x62, 0x5f, 0x5c, 0x5f, 0x20, 0x20,
    0x0b, 0x0a, 0x36, 0x36, 0x36, 0x36
};

static void test_day_profile_decode_roundtrip(void)
{
    struct fh8626_saturation_attr attr;
    uint8_t encoded[FH8626_SATURATION_CTX_BYTES];

    fh8626_saturation_decode_ctx(day_ctx, &attr);
    assert(attr.gain_mapping_enable == 1u);
    assert(attr.roll_angle == 0);
    assert(attr.value_05 == 0x00u);
    assert(attr.value_06 == 0x34u);
    assert(attr.value_07 == 0x30u);
    assert(attr.curve[0] == 0x62u);
    assert(attr.curve[11] == 0x36u);
    assert(fh8626_saturation_encode_ctx(&attr, encoded) == 0);
    assert(memcmp(encoded, day_ctx, sizeof(encoded)) == 0);
}

static void test_stock_night_transform(void)
{
    uint8_t out[FH8626_SATURATION_CTX_BYTES];
    static const uint8_t expected_curve[FH8626_SATURATION_CURVE_BYTES] = {
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };

    assert(fh8626_saturation_build_stock_night(day_ctx, day_ctx + 4, out) == 0);
    assert(out[0] == 0x01u);
    assert(out[1] == day_ctx[1]);
    assert(out[2] == day_ctx[2]);
    assert(out[3] == day_ctx[3]);
    assert(memcmp(out + 4, expected_curve, sizeof(expected_curve)) == 0);
}

static void test_signed_roll_angle(void)
{
    struct fh8626_saturation_attr attr;
    uint8_t ctx[FH8626_SATURATION_CTX_BYTES] = {0};

    attr = (struct fh8626_saturation_attr){
        .gain_mapping_enable = 1u,
        .roll_angle = -64,
        .value_05 = 1u,
        .value_06 = 2u,
        .value_07 = 3u,
    };
    assert(fh8626_saturation_encode_ctx(&attr, ctx) == 0);
    fh8626_saturation_decode_ctx(ctx, &attr);
    assert(attr.roll_angle == -64);

    attr.roll_angle = 64;
    assert(fh8626_saturation_encode_ctx(&attr, ctx) == -ERANGE);
}

int main(void)
{
    test_day_profile_decode_roundtrip();
    test_stock_night_transform();
    test_signed_roll_angle();
    return 0;
}
