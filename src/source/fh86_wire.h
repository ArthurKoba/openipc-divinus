#pragma once

#include <stddef.h>
#include <stdint.h>

#define FH86_WIRE_HEADER_SIZE 32U
#define FH86_WIRE_VERSION 1U

struct fh86_wire_frame_header {
    uint32_t payload_len;
    uint32_t flags;
    uint64_t pts_us;
    uint64_t generation;
};

enum fh86_wire_status {
    FH86_WIRE_OK = 0,
    FH86_WIRE_ERR_ARGUMENT = -1,
    FH86_WIRE_ERR_TRUNCATED = -2,
    FH86_WIRE_ERR_MAGIC = -3,
    FH86_WIRE_ERR_VERSION = -4,
    FH86_WIRE_ERR_PAYLOAD = -5
};

int fh86_wire_decode_header(const uint8_t *header, size_t header_len,
    size_t max_payload, struct fh86_wire_frame_header *out);
int fh86_wire_is_annexb(const uint8_t *data, size_t len);
