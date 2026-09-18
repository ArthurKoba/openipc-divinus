#include "fh8626_native_adapter.h"

#include <errno.h>
#include <limits.h>
#include <string.h>

static int default_copy(void *opaque, uint8_t *dst, const uint8_t *src, size_t len)
{
    (void)opaque;
    memcpy(dst, src, len);
    return 0;
}

static unsigned int find_startcode(const uint8_t *data, unsigned int offset,
    unsigned int length)
{
    unsigned int i;

    for (i = offset; i + 3u <= length; i++) {
        if (data[i] || data[i + 1])
            continue;
        if (data[i + 2] == 1u)
            return i;
        if (i + 4u <= length && !data[i + 2] && data[i + 3] == 1u)
            return i;
    }
    return length;
}

static int build_h264_pack(struct fh8626_native_adapter *adapter,
    unsigned int length, hal_vidpack *pack)
{
    unsigned int scan = 0;
    unsigned int count = 0;

    memset(pack, 0, sizeof(*pack));
    pack->data = adapter->scratch;
    pack->length = length;
    pack->timestamp = adapter->pts_us;

    while (scan < length) {
        unsigned int start = find_startcode(adapter->scratch, scan, length);
        unsigned int start_len;
        unsigned int payload;

        if (start >= length)
            break;
        if (count >= sizeof(pack->nalu) / sizeof(pack->nalu[0]))
            return -E2BIG;

        start_len = adapter->scratch[start + 2] == 1u ? 3u : 4u;
        payload = start + start_len;
        if (payload >= length)
            return -EBADMSG;

        pack->nalu[count].offset = start;
        pack->nalu[count].type = adapter->scratch[payload] & 0x1f;
        count++;
        scan = payload;
    }

    if (!count)
        return -EBADMSG;

    for (unsigned int i = 0; i < count; i++) {
        unsigned int end = i + 1u < count ? pack->nalu[i + 1u].offset : length;
        if (end <= pack->nalu[i].offset)
            return -EBADMSG;
        pack->nalu[i].length = end - pack->nalu[i].offset;
    }

    pack->naluCnt = (int)count;
    return 0;
}

int fh8626_native_adapter_init(struct fh8626_native_adapter *adapter,
    struct fh8626_stream_backend *backend, uint8_t *scratch,
    size_t scratch_capacity, fh8626_stream_copy_call copy_call,
    void *copy_opaque, uint64_t frame_interval_us)
{
    if (!adapter || !backend || !scratch || !scratch_capacity || !frame_interval_us)
        return -EINVAL;

    memset(adapter, 0, sizeof(*adapter));
    adapter->backend = backend;
    adapter->scratch = scratch;
    adapter->scratch_capacity = scratch_capacity;
    adapter->copy_call = copy_call ? copy_call : default_copy;
    adapter->copy_opaque = copy_opaque;
    adapter->frame_interval_us = frame_interval_us;
    return 0;
}

int fh8626_native_adapter_pump(struct fh8626_native_adapter *adapter,
    fh8626_video_sink sink)
{
    struct fh8626_native_stream native_stream;
    hal_vidpack pack;
    hal_vidstream stream;
    size_t total;
    int copy_ret = 0;
    int release_ret;
    int pack_ret;
    int sink_ret;

    if (!adapter || !adapter->backend || !adapter->copy_call || !sink)
        return -EINVAL;

    int ret = fh8626_stream_backend_acquire(adapter->backend, &native_stream);
    if (ret)
        return ret;

    total = (size_t)native_stream.first_len + native_stream.second_len;
    adapter->last_timestamp_raw = native_stream.timestamp_raw;

    if (!total || total > adapter->scratch_capacity || total > UINT_MAX) {
        copy_ret = -EMSGSIZE;
    } else {
        copy_ret = adapter->copy_call(adapter->copy_opaque, adapter->scratch,
            native_stream.first, native_stream.first_len);
        if (!copy_ret && native_stream.second_len)
            copy_ret = adapter->copy_call(adapter->copy_opaque,
                adapter->scratch + native_stream.first_len,
                native_stream.second, native_stream.second_len);
    }

    release_ret = fh8626_stream_backend_release(adapter->backend);
    if (release_ret)
        return release_ret;
    if (copy_ret) {
        adapter->copy_errors++;
        return copy_ret;
    }

    pack_ret = build_h264_pack(adapter, (unsigned int)total, &pack);
    if (pack_ret) {
        adapter->malformed_frames++;
        return pack_ret;
    }

    stream.pack = &pack;
    stream.count = 1;
    stream.seq = adapter->sequence++;

    sink_ret = sink((char)FH8626_NATIVE_CHANNEL, &stream);
    adapter->pts_us += adapter->frame_interval_us;
    if (sink_ret) {
        adapter->sink_errors++;
        return sink_ret;
    }

    adapter->frames_delivered++;
    return 0;
}
