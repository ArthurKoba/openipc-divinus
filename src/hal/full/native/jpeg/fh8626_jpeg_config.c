#include "fh8626_jpeg_config.h"

#include <errno.h>
#include <string.h>

const uint32_t fh_jpeg_quality_lut[10] = {
    0x10u,0x20u,0x30u,0x50u,0x70u,
    0x90u,0x110u,0x130u,0x150u,0x170u
};

static int fps_valid(uint32_t p)
{
    return (p & 0xffffu) != 0u && (p >> 16) != 0u;
}

uint32_t fh_jpeg_quality_bucket(uint32_t quality_percent)
{
    uint32_t bucket;

    if (quality_percent < 1u)
        quality_percent = 1u;
    if (quality_percent > 99u)
        quality_percent = 99u;
    bucket = quality_percent / 10u;
    return bucket > 9u ? 9u : bucket;
}

uint32_t fh_jpeg_quality_hw(uint32_t quality_percent)
{
    return fh_jpeg_quality_lut[fh_jpeg_quality_bucket(quality_percent)];
}

uint32_t fh_jpeg_quality_to_qp(uint32_t quality_percent)
{
    if (quality_percent < 1u)
        quality_percent = 1u;
    if (quality_percent > 99u)
        quality_percent = 99u;
    return 99u - quality_percent;
}

int fh_jpeg_cfg_validate_driver(const struct fh_jpeg_cfg_wire *cfg)
{
    if (!cfg || cfg->mode != 2u || !cfg->width || !cfg->height ||
        !cfg->frame_count || !cfg->frame_time)
        return -EINVAL;
    if (cfg->qp > FH_JPEG_QP_MAX || cfg->min_qp > FH_JPEG_QP_MAX ||
        cfg->max_qp > FH_JPEG_QP_MAX || cfg->rotation > 3u)
        return -EINVAL;
    return 0;
}

int fh_jpeg_cfg_validate_sdk(const struct fh_jpeg_cfg_wire *cfg)
{
    if (fh_jpeg_cfg_validate_driver(cfg))
        return -EINVAL;
    return cfg->min_qp <= cfg->max_qp ? 0 : -EINVAL;
}

int fh_jpeg_drop_validate_driver(const struct fh_jpeg_drop_wire *drop)
{
    if (!drop || !fps_valid(drop->src_fps_packed))
        return -EINVAL;
    if (drop->fixed_drop_enable && !fps_valid(drop->fixed_dst_fps_packed))
        return -EINVAL;
    if (drop->qp_drop_enable &&
        (drop->qp_threshold > FH_JPEG_QP_MAX ||
         !fps_valid(drop->qp_drop_fps_packed)))
        return -EINVAL;
    if (drop->instant_rate_enable &&
        (drop->instant_rate_threshold < 110u ||
         drop->instant_rate_threshold > 1000u ||
         !fps_valid(drop->instant_rate_fps_packed)))
        return -EINVAL;
    return 0;
}

int fh_jpeg_drop_validate_sdk(const struct fh_jpeg_drop_wire *drop)
{
    return fh_jpeg_drop_validate_driver(drop);
}

int fh_jpeg_drop_build_safe(struct fh_jpeg_drop_wire *out,
                            uint32_t src_fps_packed,
                            uint32_t fixed_dst_fps_packed,
                            uint32_t qp_threshold,
                            uint32_t qp_drop_fps_packed,
                            uint32_t instant_rate_threshold,
                            uint32_t instant_rate_fps_packed)
{
    struct fh_jpeg_drop_wire tmp;

    if (!out)
        return -EINVAL;
    memset(&tmp, 0, sizeof(tmp));
    tmp.src_fps_packed = src_fps_packed;
    tmp.fixed_drop_enable = fixed_dst_fps_packed != 0u;
    tmp.fixed_dst_fps_packed = fixed_dst_fps_packed;
    tmp.qp_drop_enable = qp_drop_fps_packed != 0u;
    tmp.qp_threshold = qp_threshold;
    tmp.qp_drop_fps_packed = qp_drop_fps_packed;
    tmp.instant_rate_enable = instant_rate_threshold != 0u;
    tmp.instant_rate_threshold = instant_rate_threshold;
    tmp.instant_rate_fps_packed = instant_rate_fps_packed;
    if (fh_jpeg_drop_validate_sdk(&tmp))
        return -EINVAL;
    *out = tmp;
    return 0;
}
