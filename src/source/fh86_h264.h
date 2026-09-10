#pragma once

#include "fh86_stream.h"
#include "../hal/types.h"

#include <stdint.h>

struct fh86_h264_pack_result {
    hal_vidpack pack;
    unsigned int nalu_count;
};

enum fh86_h264_status {
    FH86_H264_OK = 0,
    FH86_H264_ERR_ARGUMENT = -1,
    FH86_H264_ERR_SIZE = -2,
    FH86_H264_ERR_NALU = -3,
    FH86_H264_ERR_NALU_LIMIT = -4
};

int fh86_h264_build_pack(const struct fh86_stream_frame *frame,
    struct fh86_h264_pack_result *out);
