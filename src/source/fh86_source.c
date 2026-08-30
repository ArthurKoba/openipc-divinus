#define _POSIX_C_SOURCE 200809L

#include "fh86_source.h"

#include <poll.h>
#include <string.h>

static int source_should_stop(fh86_source_should_stop_fn should_stop,
    void *opaque) {
    return should_stop && should_stop(opaque);
}

static void reconnect_wait(int delay_ms,
    fh86_source_should_stop_fn should_stop, void *opaque) {
    int remaining = delay_ms;

    while (remaining > 0 && !source_should_stop(should_stop, opaque)) {
        int slice = remaining > 50 ? 50 : remaining;
        poll(NULL, 0, slice);
        remaining -= slice;
    }
}

int fh86_source_init(struct fh86_source *source,
    const struct fh86_source_config *config,
    fh86_source_frame_fn frame_fn, void *frame_opaque) {
    int status;
    size_t path_len;

    if (!source || !config || !config->path || !config->path[0] ||
        !config->max_payload || config->connect_timeout_ms < 0 ||
        config->read_timeout_ms < 0 || config->reconnect_delay_ms < 0)
        return FH86_SOURCE_ERR_ARGUMENT;

    path_len = strlen(config->path);
    if (path_len >= sizeof(source->path))
        return FH86_SOURCE_ERR_ARGUMENT;

    memset(source, 0, sizeof(*source));
    memcpy(source->path, config->path, path_len + 1);
    source->max_payload = config->max_payload;
    source->reconnect_delay_ms = config->reconnect_delay_ms;
    source->frame_fn = frame_fn;
    source->frame_opaque = frame_opaque;

    status = fh86_unix_transport_init(&source->transport,
        source->path, config->connect_timeout_ms,
        config->read_timeout_ms);
    if (status != FH86_UNIX_OK)
        return FH86_SOURCE_ERR_TRANSPORT;

    fh86_stream_reader_init(&source->reader,
        fh86_unix_transport_read, &source->transport,
        source->max_payload);

    return FH86_SOURCE_OK;
}

void fh86_source_close(struct fh86_source *source) {
    if (!source)
        return;

    fh86_unix_transport_close(&source->transport);
}

int fh86_source_run(struct fh86_source *source,
    fh86_source_should_stop_fn should_stop, void *stop_opaque) {
    int had_connection = 0;

    if (!source || !source->path[0] || !source->max_payload)
        return FH86_SOURCE_ERR_ARGUMENT;

    while (!source_should_stop(should_stop, stop_opaque)) {
        int status = fh86_unix_transport_connect(&source->transport);

        if (status != FH86_UNIX_OK) {
            source->stats.io_errors++;
            reconnect_wait(source->reconnect_delay_ms,
                should_stop, stop_opaque);
            continue;
        }

        source->stats.connections++;
        if (had_connection)
            source->stats.reconnects++;
        had_connection = 1;

        fh86_stream_reader_reset_session(&source->reader);

        while (!source_should_stop(should_stop, stop_opaque)) {
            struct fh86_stream_frame frame;

            status = fh86_stream_next(&source->reader, &frame);

            if (status == FH86_STREAM_OK) {
                source->stats.frames++;
                if (frame.generation_changed)
                    source->stats.generation_changes++;
                if (source->frame_fn)
                    source->frame_fn(source->frame_opaque, &frame);
                fh86_stream_frame_release(&frame);
                continue;
            }

            if (status == FH86_STREAM_STALE) {
                source->stats.stale_frames++;
                continue;
            }

            if (status == FH86_STREAM_ERR_TIMEOUT)
                source->stats.timeouts++;
            else if (status == FH86_STREAM_ERR_WIRE ||
                status == FH86_STREAM_ERR_ANNEXB)
                source->stats.protocol_errors++;
            else
                source->stats.io_errors++;

            break;
        }

        fh86_unix_transport_close(&source->transport);

        if (!source_should_stop(should_stop, stop_opaque))
            reconnect_wait(source->reconnect_delay_ms,
                should_stop, stop_opaque);
    }

    fh86_unix_transport_close(&source->transport);
    return FH86_SOURCE_OK;
}
