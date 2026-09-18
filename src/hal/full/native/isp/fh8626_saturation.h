#ifndef FH8626_SATURATION_H
#define FH8626_SATURATION_H

#include <stddef.h>
#include <stdint.h>

#define FH8626_SATURATION_CURVE_BYTES 12u
#define FH8626_SATURATION_CTX_BYTES 16u

/*
 * Exact public API_ISP_{Get,Set}Saturation record recovered from Apollo.
 * The setter packs this 20-byte public object into shared ISP context
 * ctx+0x2d8..0x2e7.
 */
struct fh8626_saturation_attr {
    uint32_t gain_mapping_enable;
    int8_t roll_angle;
    uint8_t value_05;
    uint8_t value_06;
    uint8_t value_07;
    uint8_t curve[FH8626_SATURATION_CURVE_BYTES];
};

_Static_assert(sizeof(struct fh8626_saturation_attr) == 20u,
    "FH8626 saturation public ABI size");
_Static_assert(offsetof(struct fh8626_saturation_attr, curve) == 8u,
    "FH8626 saturation curve offset");

void fh8626_saturation_decode_ctx(
    const uint8_t ctx[FH8626_SATURATION_CTX_BYTES],
    struct fh8626_saturation_attr *attr);
int fh8626_saturation_encode_ctx(
    const struct fh8626_saturation_attr *attr,
    uint8_t ctx[FH8626_SATURATION_CTX_BYTES]);

/*
 * Stock night service calls its saturation wrapper as (mode=1, value=1).
 * mode=1 keeps angle/value bytes, enables gain mapping, and offsets the
 * profile-captured 12-byte baseline so baseline[0] becomes target value 1,
 * with byte saturation to [0,255].
 */
int fh8626_saturation_build_stock_night(
    const uint8_t current_ctx[FH8626_SATURATION_CTX_BYTES],
    const uint8_t baseline[FH8626_SATURATION_CURVE_BYTES],
    uint8_t out_ctx[FH8626_SATURATION_CTX_BYTES]);

#endif
