#include "fh86_h264.h"

#include "../fmt/nal.h"

#include <limits.h>
#include <string.h>

int fh86_h264_build_pack(const struct fh86_stream_frame *frame,
    struct fh86_h264_pack_result *out) {
    unsigned int scan_offset = 0;
    unsigned int frame_len;
    unsigned int count = 0;

    if (!frame || !out || !frame->data || !frame->len)
        return FH86_H264_ERR_ARGUMENT;
    if (frame->len > UINT_MAX)
        return FH86_H264_ERR_SIZE;

    frame_len = (unsigned int)frame->len;
    memset(out, 0, sizeof(*out));

    out->pack.data = frame->data;
    out->pack.length = frame_len;
    out->pack.offset = 0;
    out->pack.timestamp = frame->pts_us;

    while (scan_offset < frame_len) {
        unsigned int start = nal_find_startcode(frame->data,
            scan_offset, frame_len);
        unsigned int start_len;
        unsigned int payload_pos;

        if (start >= frame_len)
            break;
        if (count >= sizeof(out->pack.nalu) / sizeof(out->pack.nalu[0]))
            return FH86_H264_ERR_NALU_LIMIT;

        start_len = frame->data[start + 2] == 1 ? 3u : 4u;
        payload_pos = start + start_len;
        if (payload_pos >= frame_len)
            return FH86_H264_ERR_NALU;

        out->pack.nalu[count].offset = start;
        out->pack.nalu[count].type = frame->data[payload_pos] & 0x1f;
        count++;
        scan_offset = payload_pos;
    }

    if (!count)
        return FH86_H264_ERR_NALU;

    for (unsigned int i = 0; i < count; i++) {
        unsigned int end = i + 1 < count ?
            out->pack.nalu[i + 1].offset : frame_len;

        if (end <= out->pack.nalu[i].offset)
            return FH86_H264_ERR_NALU;
        out->pack.nalu[i].length = end - out->pack.nalu[i].offset;
    }

    out->pack.naluCnt = (int)count;
    out->nalu_count = count;
    return FH86_H264_OK;
}
