#pragma once

#include "fh8626_native_adapter.h"
#include "fh8626_provider.h"
#include "fh8626_native_runtime.h"
#include "../types.h"

#define FH8626_VENC_CHN_NUM 1

extern hal_chnstate fh8626_state[FH8626_VENC_CHN_NUM];

struct fh8626_stub_stats {
    unsigned media_calls;
    unsigned step_calls;
    unsigned frames_delivered;
    unsigned sink_errors;
    unsigned leases_started;
    unsigned leases_released;
    fh8626_lifecycle_state life_state;
};

int fh8626_hal_stub_enabled(void);
int fh8626_hal_production_ready(void);
int fh8626_hal_stub_get_stats(struct fh8626_stub_stats *stats);
int fh8626_sdk_start(fh8626_video_sink sink);
int fh8626_sdk_stop(void);
