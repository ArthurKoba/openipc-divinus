#include "fh86_stream.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

static int read_exact(struct fh86_stream_reader *reader, uint8_t *buffer,
    size_t length, int empty_is_eof) {
    size_t offset = 0;

    while (offset < length) {
        ssize_t count = reader->read_fn(reader->opaque,
            buffer + offset, length - offset);

        if (count < 0) {
            if (errno == ETIMEDOUT || errno == EAGAIN ||
                errno == EWOULDBLOCK)
                return FH86_STREAM_ERR_TIMEOUT;
            return FH86_STREAM_ERR_IO;
        }

        if (count == 0) {
            if (offset == 0 && empty_is_eof)
                return FH86_STREAM_ERR_EOF;
            return FH86_STREAM_ERR_TRUNCATED;
        }

        if ((size_t)count > length - offset)
            return FH86_STREAM_ERR_IO;

        offset += (size_t)count;
    }

    return FH86_STREAM_OK;
}

void fh86_stream_reader_init(struct fh86_stream_reader *reader,
    fh86_stream_read_fn read_fn, void *opaque, size_t max_payload) {
    if (!reader)
        return;

    memset(reader, 0, sizeof(*reader));
    reader->read_fn = read_fn;
    reader->opaque = opaque;
    reader->max_payload = max_payload;
}

void fh86_stream_reader_reset_session(struct fh86_stream_reader *reader) {
    if (!reader)
        return;

    reader->generation = 0;
    reader->have_generation = 0;
}

void fh86_stream_frame_release(struct fh86_stream_frame *frame) {
    if (!frame)
        return;

    free(frame->data);
    memset(frame, 0, sizeof(*frame));
}

int fh86_stream_next(struct fh86_stream_reader *reader,
    struct fh86_stream_frame *frame) {
    uint8_t header[FH86_WIRE_HEADER_SIZE];
    struct fh86_wire_frame_header decoded;
    uint8_t *payload;
    int status;

    if (!reader || !reader->read_fn || !reader->max_payload || !frame)
        return FH86_STREAM_ERR_ARGUMENT;

    memset(frame, 0, sizeof(*frame));

    status = read_exact(reader, header, sizeof(header), 1);
    if (status != FH86_STREAM_OK)
        return status;

    status = fh86_wire_decode_header(header, sizeof(header),
        reader->max_payload, &decoded);
    if (status != FH86_WIRE_OK)
        return FH86_STREAM_ERR_WIRE;

    payload = malloc(decoded.payload_len);
    if (!payload)
        return FH86_STREAM_ERR_ALLOC;

    status = read_exact(reader, payload, decoded.payload_len, 0);
    if (status != FH86_STREAM_OK) {
        free(payload);
        return status;
    }

    if (!fh86_wire_is_annexb(payload, decoded.payload_len)) {
        free(payload);
        return FH86_STREAM_ERR_ANNEXB;
    }

    if (reader->have_generation &&
        decoded.generation < reader->generation) {
        free(payload);
        return FH86_STREAM_STALE;
    }

    frame->data = payload;
    frame->len = decoded.payload_len;
    frame->flags = decoded.flags;
    frame->pts_us = decoded.pts_us;
    frame->generation = decoded.generation;

    if (!reader->have_generation ||
        decoded.generation > reader->generation) {
        frame->generation_changed = 1;
        reader->generation = decoded.generation;
        reader->have_generation = 1;
    }

    return FH86_STREAM_OK;
}
