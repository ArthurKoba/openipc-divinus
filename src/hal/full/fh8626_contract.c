#include "fh8626_contract.h"

#include <errno.h>
#include <string.h>

void fh8626_lifecycle_reset(struct fh8626_lifecycle *life)
{
    if (!life)
        return;
    memset(life, 0, sizeof(*life));
    life->state = FH8626_LIFE_COLD;
}

int fh8626_lifecycle_apply(struct fh8626_lifecycle *life, fh8626_lifecycle_event event)
{
    if (!life)
        return -EINVAL;

    switch (event) {
        case FH8626_LIFE_HAL_INIT:
            if (life->state != FH8626_LIFE_COLD) return -EPERM;
            life->state = FH8626_LIFE_HAL_READY;
            return 0;
        case FH8626_LIFE_SYSTEM_INIT:
            if (life->state != FH8626_LIFE_HAL_READY) return -EPERM;
            life->state = FH8626_LIFE_SYSTEM_READY;
            return 0;
        case FH8626_LIFE_PIPELINE_CREATE:
            if (life->state != FH8626_LIFE_SYSTEM_READY) return -EPERM;
            life->state = FH8626_LIFE_PIPELINE_READY;
            return 0;
        case FH8626_LIFE_VIDEO_CREATE:
            if (life->state != FH8626_LIFE_PIPELINE_READY) return -EPERM;
            life->state = FH8626_LIFE_VIDEO_READY;
            return 0;
        case FH8626_LIFE_STREAM_START:
            if (life->state != FH8626_LIFE_VIDEO_READY) return -EPERM;
            life->state = FH8626_LIFE_STREAMING;
            return 0;
        case FH8626_LIFE_STREAM_STOP:
            if (life->state != FH8626_LIFE_STREAMING || life->lease_held) return -EBUSY;
            life->state = FH8626_LIFE_VIDEO_READY;
            return 0;
        case FH8626_LIFE_VIDEO_DESTROY:
            if (life->state != FH8626_LIFE_VIDEO_READY || life->lease_held) return -EBUSY;
            life->state = FH8626_LIFE_PIPELINE_READY;
            return 0;
        case FH8626_LIFE_PIPELINE_DESTROY:
            if (life->state != FH8626_LIFE_PIPELINE_READY) return -EPERM;
            life->state = FH8626_LIFE_SYSTEM_READY;
            return 0;
        case FH8626_LIFE_SYSTEM_DEINIT:
            if (life->state != FH8626_LIFE_SYSTEM_READY) return -EPERM;
            life->state = FH8626_LIFE_HAL_READY;
            return 0;
        case FH8626_LIFE_HAL_DEINIT:
            if (life->state != FH8626_LIFE_HAL_READY) return -EPERM;
            life->state = FH8626_LIFE_COLD;
            return 0;
    }
    return -EINVAL;
}

int fh8626_lifecycle_lease_begin(struct fh8626_lifecycle *life)
{
    if (!life || life->state != FH8626_LIFE_STREAMING)
        return -EPERM;
    if (life->lease_held)
        return -EBUSY;
    life->lease_held = 1;
    life->leases_started++;
    return 0;
}

int fh8626_lifecycle_lease_release(struct fh8626_lifecycle *life)
{
    if (!life || !life->lease_held)
        return -EPERM;
    life->lease_held = 0;
    life->leases_released++;
    return 0;
}

int fh8626_lifecycle_balanced(const struct fh8626_lifecycle *life)
{
    if (!life)
        return 0;
    return !life->lease_held && life->leases_started == life->leases_released;
}

int fh8626_stream_decode(const uint32_t desc[FH8626_MEDIA_STREAM_DESC_WORDS], uintptr_t ring_base,
    uint32_t ring_size, struct fh8626_stream_span *span)
{
    uintptr_t virt;
    uint32_t len;
    uint64_t ring_end;
    uint32_t tail;

    if (!desc || !span || !ring_base || !ring_size)
        return -EINVAL;
    if (desc[1] != FH8626_MEDIA_STREAM_KIND)
        return -EAGAIN;

    virt = (uintptr_t)desc[7];
    len = desc[8];
    ring_end = (uint64_t)ring_base + ring_size;
    if (!len || len > ring_size || virt < ring_base || (uint64_t)virt >= ring_end)
        return -ERANGE;

    memset(span, 0, sizeof(*span));
    span->timestamp_raw = desc[10];
    tail = (uint32_t)(ring_end - virt);
    span->first = virt;
    if (len <= tail) {
        span->first_len = len;
        return 0;
    }

    span->first_len = tail;
    span->second = ring_base;
    span->second_len = len - tail;
    return 0;
}

int fh8626_video_contract_known(const hal_vidconfig *config)
{
    if (!config)
        return 0;
    if (config->width != FH8626_NATIVE_WIDTH || config->height != FH8626_NATIVE_HEIGHT)
        return 0;
    if (config->codec != HAL_VIDCODEC_H264)
        return 0;
    if (config->framerate != FH8626_NATIVE_FPS || config->gop != 25u)
        return 0;
    if (config->profile != HAL_VIDPROFILE_BASELINE)
        return 0;
    if (config->mode != HAL_VIDMODE_CBR &&
        config->mode != HAL_VIDMODE_VBR &&
        config->mode != HAL_VIDMODE_QP &&
        config->mode != HAL_VIDMODE_AVBR)
        return 0;
    if (!config->bitrate)
        return 0;
    return 1;
}

uint32_t fh8626_timestamp_us_to_rtp90(uint64_t timestamp_us)
{
    return (uint32_t)((timestamp_us * 90u) / 1000u);
}

const char *fh8626_capability_state_name(fh8626_capability_state state)
{
    switch (state) {
        case FH8626_CAP_PROVEN: return "proven";
        case FH8626_CAP_UNRESOLVED: return "unresolved";
        case FH8626_CAP_UNSUPPORTED: return "unsupported";
    }
    return "unknown";
}

struct fh8626_capabilities fh8626_capabilities_current(void)
{
    struct fh8626_capabilities caps;
    caps.h264_720p25 = FH8626_CAP_PROVEN;
    caps.stream_lease_release = FH8626_CAP_PROVEN;
    caps.sensor_gc1054_init_order = FH8626_CAP_PROVEN;
    caps.sensor_open_backend = FH8626_CAP_UNRESOLVED;
    caps.isp_direct_kernel_bringup = FH8626_CAP_PROVEN;
    caps.same_boot_full_teardown = FH8626_CAP_UNRESOLVED;
    caps.idr_request = FH8626_CAP_PROVEN;
    caps.rate_control_mapping = FH8626_CAP_PROVEN;
    caps.vpss_1080p_scaling = FH8626_CAP_UNRESOLVED;
    caps.h265 = FH8626_CAP_UNSUPPORTED;
    caps.jpeg_snapshot = FH8626_CAP_UNRESOLVED;
    caps.mjpeg = FH8626_CAP_UNRESOLVED;
    caps.audio_rtx_transport = FH8626_CAP_PROVEN;
    caps.audio = FH8626_CAP_UNRESOLVED;
    caps.runtime_audio_reconfigure = FH8626_CAP_UNRESOLVED;
    caps.runtime_video_reconfigure = FH8626_CAP_UNRESOLVED;
    caps.temperature = FH8626_CAP_UNSUPPORTED;
    return caps;
}
