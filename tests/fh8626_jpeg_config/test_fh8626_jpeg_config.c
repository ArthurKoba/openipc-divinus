#include "fh8626_jpeg_config.h"

#include <assert.h>
#include <stdint.h>

static void test_quality_mapping(void)
{
    assert(fh_jpeg_quality_bucket(1u) == 0u);
    assert(fh_jpeg_quality_bucket(9u) == 0u);
    assert(fh_jpeg_quality_bucket(10u) == 1u);
    assert(fh_jpeg_quality_bucket(99u) == 9u);
    assert(fh_jpeg_quality_hw(99u) == fh_jpeg_quality_lut[9]);
    assert(fh_jpeg_quality_to_qp(1u) == 98u);
    assert(fh_jpeg_quality_to_qp(99u) == 0u);
}

static void test_mjpeg_validation(void)
{
    struct fh_jpeg_cfg_wire cfg = {
        .mode = 2u,
        .width = 1280u,
        .height = 720u,
        .frame_count = 25u,
        .frame_time = 1u,
        .rc_selector = 1u,
        .qp = 49u,
        .target_rate = 1024000u,
        .min_qp = 0u,
        .max_qp = FH_JPEG_QP_MAX,
        .jpeg_mode_bit = 1u,
        .quality_hw = 0x90u,
        .rotation = 0u,
    };

    assert(fh_jpeg_cfg_validate_sdk(&cfg) == 0);
    cfg.max_qp = FH_JPEG_QP_MAX + 1u;
    assert(fh_jpeg_cfg_validate_sdk(&cfg) != 0);
}

int main(void)
{
    test_quality_mapping();
    test_mjpeg_validation();
    return 0;
}
