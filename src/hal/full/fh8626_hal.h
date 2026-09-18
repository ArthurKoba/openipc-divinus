#pragma once

#include "fh8626_native_adapter.h"
#include "fh8626_provider.h"
#include "fh8626_native_runtime.h"
#include "native/osd/fh8626_graphv2.h"
#include "../types.h"

#define FH8626_VENC_CHN_NUM 2

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
int fh8626_native_active(void);
int fh8626_request_idr(void);
int fh8626_set_bitrate(uint32_t bitrate_kbps);
int fh8626_set_mirror_flip(int mirror, int flip);
int fh8626_set_grayscale(int enabled);
int fh8626_set_antiflicker(int hz);
int fh8626_jpeg_init(uint32_t mode, uint32_t width, uint32_t height,
    uint32_t quality, uint32_t fps, uint32_t bitrate, uint32_t rc_mode);
void fh8626_jpeg_deinit(void);
void fh8626_jpeg_deinit_mode(uint32_t mode);
int fh8626_jpeg_get(uint32_t width, uint32_t height, uint32_t quality,
    hal_jpegdata *jpeg);

int fh8626_region_create(unsigned id, hal_rect rect, uint8_t opacity);
int fh8626_region_setbitmap(unsigned id, const hal_bitmap *bitmap);
int fh8626_region_destroy(unsigned id);
