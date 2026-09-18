#pragma once

#include "fh8626_stream_backend.h"
#include "../types.h"

#include <stddef.h>
#include <stdint.h>

typedef int (*fh8626_stream_copy_call)(void *opaque, uint8_t *dst,
    const uint8_t *src, size_t len);
typedef int (*fh8626_video_sink)(char index, hal_vidstream *stream);

struct fh8626_native_adapter {
    struct fh8626_stream_backend *backend;
    uint8_t *scratch;
    size_t scratch_capacity;
    fh8626_stream_copy_call copy_call;
    void *copy_opaque;
    unsigned int sequence;
    uint64_t pts_us;
    uint64_t frame_interval_us;
    uint32_t last_timestamp_raw;
    uint64_t frames_delivered;
    uint64_t malformed_frames;
    uint64_t copy_errors;
    uint64_t sink_errors;
};

int fh8626_native_adapter_init(struct fh8626_native_adapter *adapter,
    struct fh8626_stream_backend *backend, uint8_t *scratch,
    size_t scratch_capacity, fh8626_stream_copy_call copy_call,
    void *copy_opaque, uint64_t frame_interval_us);
int fh8626_native_adapter_pump(struct fh8626_native_adapter *adapter,
    fh8626_video_sink sink);
