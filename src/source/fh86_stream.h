#pragma once

#include "fh86_wire.h"

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

typedef ssize_t (*fh86_stream_read_fn)(void *opaque, void *buffer, size_t length);

struct fh86_stream_reader {
    fh86_stream_read_fn read_fn;
    void *opaque;
    size_t max_payload;
    uint64_t generation;
    int have_generation;
};

struct fh86_stream_frame {
    uint8_t *data;
    size_t len;
    uint32_t flags;
    uint64_t pts_us;
    uint64_t generation;
    int generation_changed;
};

enum fh86_stream_status {
    FH86_STREAM_OK = 0,
    FH86_STREAM_STALE = 1,
    FH86_STREAM_ERR_ARGUMENT = -1,
    FH86_STREAM_ERR_IO = -2,
    FH86_STREAM_ERR_EOF = -3,
    FH86_STREAM_ERR_TRUNCATED = -4,
    FH86_STREAM_ERR_WIRE = -5,
    FH86_STREAM_ERR_ALLOC = -6,
    FH86_STREAM_ERR_ANNEXB = -7,
    FH86_STREAM_ERR_TIMEOUT = -8
};

void fh86_stream_reader_init(struct fh86_stream_reader *reader,
    fh86_stream_read_fn read_fn, void *opaque, size_t max_payload);

void fh86_stream_reader_reset_session(struct fh86_stream_reader *reader);

int fh86_stream_next(struct fh86_stream_reader *reader,
    struct fh86_stream_frame *frame);

void fh86_stream_frame_release(struct fh86_stream_frame *frame);
