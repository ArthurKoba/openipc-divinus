#include "../../src/source/fh86_wire.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void put_be32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static void put_be64(uint8_t *p, uint64_t value) {
    put_be32(p, (uint32_t)(value >> 32));
    put_be32(p + 4, (uint32_t)value);
}

static void make_header(uint8_t *header) {
    memset(header, 0, FH86_WIRE_HEADER_SIZE);
    memcpy(header, "FH86", 4);
    put_be32(header + 4, FH86_WIRE_VERSION);
    put_be32(header + 8, 4096);
    put_be32(header + 12, 0xA5A55A5A);
    put_be64(header + 16, UINT64_C(1234567890123));
    put_be64(header + 24, UINT64_C(0x0102030405060708));
}

int main(void) {
    uint8_t header[FH86_WIRE_HEADER_SIZE];
    struct fh86_wire_frame_header decoded;

    make_header(header);

    assert(fh86_wire_decode_header(header, sizeof(header),
        1024 * 1024, &decoded) == FH86_WIRE_OK);
    assert(decoded.payload_len == 4096);
    assert(decoded.flags == UINT32_C(0xA5A55A5A));
    assert(decoded.pts_us == UINT64_C(1234567890123));
    assert(decoded.generation == UINT64_C(0x0102030405060708));

    assert(fh86_wire_decode_header(header, 31,
        1024 * 1024, &decoded) == FH86_WIRE_ERR_TRUNCATED);

    header[0] = 'X';
    assert(fh86_wire_decode_header(header, sizeof(header),
        1024 * 1024, &decoded) == FH86_WIRE_ERR_MAGIC);

    make_header(header);
    put_be32(header + 4, 2);
    assert(fh86_wire_decode_header(header, sizeof(header),
        1024 * 1024, &decoded) == FH86_WIRE_ERR_VERSION);

    make_header(header);
    put_be32(header + 8, 0);
    assert(fh86_wire_decode_header(header, sizeof(header),
        1024 * 1024, &decoded) == FH86_WIRE_ERR_PAYLOAD);

    make_header(header);
    put_be32(header + 8, 1024 * 1024 + 1);
    assert(fh86_wire_decode_header(header, sizeof(header),
        1024 * 1024, &decoded) == FH86_WIRE_ERR_PAYLOAD);

    {
        const uint8_t annexb4[] = {0, 0, 0, 1, 0x67};
        const uint8_t annexb3[] = {0, 0, 1, 0x65};
        const uint8_t bad[] = {0, 0, 2, 1, 0x65};

        assert(fh86_wire_is_annexb(annexb4, sizeof(annexb4)) == 1);
        assert(fh86_wire_is_annexb(annexb3, sizeof(annexb3)) == 1);
        assert(fh86_wire_is_annexb(bad, sizeof(bad)) == 0);
    }

    puts("fh86_wire PASS");
    return 0;
}
