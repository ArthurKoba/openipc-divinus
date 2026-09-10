#include "../../src/source/fh86_h264.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void test_mixed_start_codes(void) {
    uint8_t payload[] = {
        0, 0, 0, 1, 0x67, 0x11,
        0, 0, 1, 0x68, 0x22, 0x33,
        0, 0, 0, 1, 0x65, 0x44, 0x55, 0x66
    };
    struct fh86_stream_frame frame;
    struct fh86_h264_pack_result result;

    memset(&frame, 0, sizeof(frame));
    frame.data = payload;
    frame.len = sizeof(payload);
    frame.pts_us = UINT64_C(1234567);

    assert(fh86_h264_build_pack(&frame, &result) == FH86_H264_OK);
    assert(result.nalu_count == 3);
    assert(result.pack.naluCnt == 3);
    assert(result.pack.data == payload);
    assert(result.pack.length == sizeof(payload));
    assert(result.pack.offset == 0);
    assert(result.pack.timestamp == UINT64_C(1234567));

    assert(result.pack.nalu[0].offset == 0);
    assert(result.pack.nalu[0].length == 6);
    assert(result.pack.nalu[0].type == 7);

    assert(result.pack.nalu[1].offset == 6);
    assert(result.pack.nalu[1].length == 6);
    assert(result.pack.nalu[1].type == 8);

    assert(result.pack.nalu[2].offset == 12);
    assert(result.pack.nalu[2].length == 8);
    assert(result.pack.nalu[2].type == 5);
}

static void test_nalu_limit(void) {
    uint8_t payload[9 * 5];
    struct fh86_stream_frame frame;
    struct fh86_h264_pack_result result;

    for (unsigned int i = 0; i < 9; i++) {
        payload[i * 5 + 0] = 0;
        payload[i * 5 + 1] = 0;
        payload[i * 5 + 2] = 0;
        payload[i * 5 + 3] = 1;
        payload[i * 5 + 4] = 1;
    }

    memset(&frame, 0, sizeof(frame));
    frame.data = payload;
    frame.len = sizeof(payload);

    assert(fh86_h264_build_pack(&frame, &result) ==
        FH86_H264_ERR_NALU_LIMIT);
}

static void test_invalid_tail(void) {
    uint8_t payload[] = {0, 0, 0, 1};
    struct fh86_stream_frame frame;
    struct fh86_h264_pack_result result;

    memset(&frame, 0, sizeof(frame));
    frame.data = payload;
    frame.len = sizeof(payload);

    assert(fh86_h264_build_pack(&frame, &result) == FH86_H264_ERR_NALU);
}

int main(void) {
    test_mixed_start_codes();
    test_nalu_limit();
    test_invalid_tail();

    puts("fh86_h264 PASS");
    return 0;
}
