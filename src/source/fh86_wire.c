#include "fh86_wire.h"

static uint32_t read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
        ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint64_t read_be64(const uint8_t *p) {
    return ((uint64_t)read_be32(p) << 32) | read_be32(p + 4);
}

int fh86_wire_decode_header(const uint8_t *header, size_t header_len,
    size_t max_payload, struct fh86_wire_frame_header *out) {
    if (!header || !out || !max_payload)
        return FH86_WIRE_ERR_ARGUMENT;
    if (header_len < FH86_WIRE_HEADER_SIZE)
        return FH86_WIRE_ERR_TRUNCATED;
    if (header[0] != 'F' || header[1] != 'H' ||
        header[2] != '8' || header[3] != '6')
        return FH86_WIRE_ERR_MAGIC;
    if (read_be32(header + 4) != FH86_WIRE_VERSION)
        return FH86_WIRE_ERR_VERSION;

    out->payload_len = read_be32(header + 8);
    out->flags = read_be32(header + 12);
    out->pts_us = read_be64(header + 16);
    out->generation = read_be64(header + 24);

    if (!out->payload_len || out->payload_len > max_payload)
        return FH86_WIRE_ERR_PAYLOAD;

    return FH86_WIRE_OK;
}

int fh86_wire_is_annexb(const uint8_t *data, size_t len) {
    if (!data || len < 4)
        return 0;
    if (data[0] != 0 || data[1] != 0)
        return 0;
    if (data[2] == 1)
        return 1;
    return len >= 5 && data[2] == 0 && data[3] == 1;
}
