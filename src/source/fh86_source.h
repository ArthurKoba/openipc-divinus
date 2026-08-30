#pragma once

#include "fh86_stream.h"
#include "fh86_unix.h"

#include <stddef.h>
#include <stdint.h>

struct fh86_source_config {
    const char *path;
    size_t max_payload;
    int connect_timeout_ms;
    int read_timeout_ms;
    int reconnect_delay_ms;
};

struct fh86_source_stats {
    uint64_t connections;
    uint64_t reconnects;
    uint64_t frames;
    uint64_t stale_frames;
    uint64_t generation_changes;
    uint64_t timeouts;
    uint64_t protocol_errors;
    uint64_t io_errors;
};

typedef void (*fh86_source_frame_fn)(void *opaque,
    const struct fh86_stream_frame *frame);
typedef int (*fh86_source_should_stop_fn)(void *opaque);

struct fh86_source {
    struct fh86_unix_transport transport;
    struct fh86_stream_reader reader;
    struct fh86_source_stats stats;
    fh86_source_frame_fn frame_fn;
    void *frame_opaque;
    char path[sizeof(((struct sockaddr_un *)0)->sun_path)];
    size_t max_payload;
    int reconnect_delay_ms;
};

enum fh86_source_status {
    FH86_SOURCE_OK = 0,
    FH86_SOURCE_ERR_ARGUMENT = -1,
    FH86_SOURCE_ERR_TRANSPORT = -2
};

int fh86_source_init(struct fh86_source *source,
    const struct fh86_source_config *config,
    fh86_source_frame_fn frame_fn, void *frame_opaque);

int fh86_source_run(struct fh86_source *source,
    fh86_source_should_stop_fn should_stop, void *stop_opaque);

void fh86_source_close(struct fh86_source *source);
