#pragma once

#include "fh8626_native_abi.h"
#include "../types.h"

#include <stddef.h>
#include <stdint.h>

typedef enum {
    FH8626_LIFE_COLD = 0,
    FH8626_LIFE_HAL_READY,
    FH8626_LIFE_SYSTEM_READY,
    FH8626_LIFE_PIPELINE_READY,
    FH8626_LIFE_VIDEO_READY,
    FH8626_LIFE_STREAMING
} fh8626_lifecycle_state;

typedef enum {
    FH8626_LIFE_HAL_INIT = 0,
    FH8626_LIFE_SYSTEM_INIT,
    FH8626_LIFE_PIPELINE_CREATE,
    FH8626_LIFE_VIDEO_CREATE,
    FH8626_LIFE_STREAM_START,
    FH8626_LIFE_STREAM_STOP,
    FH8626_LIFE_VIDEO_DESTROY,
    FH8626_LIFE_PIPELINE_DESTROY,
    FH8626_LIFE_SYSTEM_DEINIT,
    FH8626_LIFE_HAL_DEINIT
} fh8626_lifecycle_event;

typedef enum {
    FH8626_CAP_PROVEN = 0,
    FH8626_CAP_UNRESOLVED,
    FH8626_CAP_UNSUPPORTED
} fh8626_capability_state;

struct fh8626_lifecycle {
    fh8626_lifecycle_state state;
    unsigned leases_started;
    unsigned leases_released;
    int lease_held;
};

struct fh8626_stream_span {
    uintptr_t first;
    uint32_t first_len;
    uintptr_t second;
    uint32_t second_len;
    uint32_t timestamp_raw;
};

struct fh8626_capabilities {
    fh8626_capability_state h264_720p25;
    fh8626_capability_state stream_lease_release;
    fh8626_capability_state sensor_gc1054_init_order;
    fh8626_capability_state isp_direct_kernel_bringup;
    fh8626_capability_state same_boot_full_teardown;
    fh8626_capability_state idr_request;
    fh8626_capability_state rate_control_mapping;
    fh8626_capability_state vpss_1080p_scaling;
    fh8626_capability_state h265;
    fh8626_capability_state jpeg_snapshot;
    fh8626_capability_state mjpeg;
    fh8626_capability_state audio;
    fh8626_capability_state temperature;
};

void fh8626_lifecycle_reset(struct fh8626_lifecycle *life);
int fh8626_lifecycle_apply(struct fh8626_lifecycle *life, fh8626_lifecycle_event event);
int fh8626_lifecycle_lease_begin(struct fh8626_lifecycle *life);
int fh8626_lifecycle_lease_release(struct fh8626_lifecycle *life);
int fh8626_lifecycle_balanced(const struct fh8626_lifecycle *life);

int fh8626_stream_decode(const uint32_t desc[FH8626_MEDIA_STREAM_DESC_WORDS], uintptr_t ring_base,
    uint32_t ring_size, struct fh8626_stream_span *span);
int fh8626_video_contract_known(const hal_vidconfig *config);
struct fh8626_capabilities fh8626_capabilities_current(void);
const char *fh8626_capability_state_name(fh8626_capability_state state);
