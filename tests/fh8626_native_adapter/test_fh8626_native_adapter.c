#include "fh8626_native_adapter.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#define FAKE_BASE 0x10000000u
#define FAKE_RING 128u

struct fake_kernel {
    uint8_t ring[FAKE_RING];
    uint32_t desc[FH8626_MEDIA_STREAM_DESC_WORDS];
    int media_ret;
    int step_ret;
    int copy_ret;
    unsigned media_calls;
    unsigned step_calls;
    unsigned copy_calls;
};

static struct fh8626_stream_backend *sink_backend;
static uint8_t expected_frame[64];
static unsigned expected_len;
static unsigned sink_calls;
static int sink_result;

static int fake_ioctl(void *opaque, int fd, unsigned long request, void *arg)
{
    struct fake_kernel *fake = opaque;
    (void)fd;

    if (request == FH8626_MEDIA_STREAM_6) {
        fake->media_calls++;
        if (fake->media_ret)
            return fake->media_ret;
        memcpy(arg, fake->desc, sizeof(fake->desc));
        return 0;
    }
    if (request == FH8626_PAE_STREAM_STEP) {
        uint32_t *channel = arg;
        fake->step_calls++;
        assert(*channel == FH8626_NATIVE_CHANNEL);
        return fake->step_ret;
    }
    return -ENOTTY;
}

static int fake_copy(void *opaque, uint8_t *dst, const uint8_t *src, size_t len)
{
    struct fake_kernel *fake = opaque;
    uintptr_t address = (uintptr_t)src;
    size_t offset;

    fake->copy_calls++;
    if (fake->copy_ret)
        return fake->copy_ret;
    assert(address >= FAKE_BASE);
    offset = (size_t)(address - FAKE_BASE);
    assert(offset + len <= FAKE_RING);
    memcpy(dst, fake->ring + offset, len);
    return 0;
}

static int fake_sink(char index, hal_vidstream *stream)
{
    hal_vidpack *pack;

    sink_calls++;
    assert(index == FH8626_NATIVE_CHANNEL);
    assert(fh8626_stream_backend_balanced(sink_backend));
    assert(stream && stream->count == 1u);
    pack = stream->pack;
    assert(pack != NULL);
    assert(pack->length == expected_len);
    assert(!memcmp(pack->data, expected_frame, expected_len));
    assert(pack->naluCnt == 3);
    assert(pack->nalu[0].type == 7);
    assert(pack->nalu[1].type == 8);
    assert(pack->nalu[2].type == 5);
    return sink_result;
}

static unsigned make_frame(uint8_t *out)
{
    static const uint8_t frame[] = {
        0x00,0x00,0x00,0x01,0x67,0x42,0x00,0x1f,
        0x00,0x00,0x00,0x01,0x68,0xce,0x06,0xe2,
        0x00,0x00,0x00,0x01,0x65,0x88,0x84,0x21
    };
    memcpy(out, frame, sizeof(frame));
    return (unsigned)sizeof(frame);
}

static void prepare(struct fake_kernel *fake, uint32_t offset, int wrap)
{
    unsigned len;
    uint8_t frame[64];

    memset(fake, 0, sizeof(*fake));
    len = make_frame(frame);
    memcpy(expected_frame, frame, len);
    expected_len = len;

    if (!wrap) {
        memcpy(fake->ring + offset, frame, len);
    } else {
        unsigned tail = FAKE_RING - offset;
        memcpy(fake->ring + offset, frame, tail);
        memcpy(fake->ring, frame + tail, len - tail);
    }

    fake->desc[1] = FH8626_MEDIA_STREAM_KIND;
    fake->desc[6] = 0x22000000u;
    fake->desc[7] = FAKE_BASE + offset;
    fake->desc[8] = len;
    fake->desc[10] = 0xaabbccddu;
}

static void life_to_streaming(struct fh8626_lifecycle *life)
{
    fh8626_lifecycle_reset(life);
    assert(fh8626_lifecycle_apply(life, FH8626_LIFE_HAL_INIT) == 0);
    assert(fh8626_lifecycle_apply(life, FH8626_LIFE_SYSTEM_INIT) == 0);
    assert(fh8626_lifecycle_apply(life, FH8626_LIFE_PIPELINE_CREATE) == 0);
    assert(fh8626_lifecycle_apply(life, FH8626_LIFE_VIDEO_CREATE) == 0);
    assert(fh8626_lifecycle_apply(life, FH8626_LIFE_STREAM_START) == 0);
}

static void init_all(struct fake_kernel *fake, struct fh8626_lifecycle *life,
    struct fh8626_stream_backend *backend, struct fh8626_native_adapter *adapter,
    uint8_t *scratch, size_t scratch_size)
{
    life_to_streaming(life);
    assert(fh8626_stream_backend_init(backend, 4, 5, FAKE_BASE, FAKE_RING,
        fake_ioctl, fake, life) == 0);
    assert(fh8626_native_adapter_init(adapter, backend, scratch, scratch_size,
        fake_copy, fake, 40000u) == 0);
    sink_backend = backend;
    sink_calls = 0;
    sink_result = 0;
}

static void test_contiguous_and_wrap(void)
{
    struct fake_kernel fake;
    struct fh8626_lifecycle life;
    struct fh8626_stream_backend backend;
    struct fh8626_native_adapter adapter;
    uint8_t scratch[64];

    prepare(&fake, 16u, 0);
    init_all(&fake, &life, &backend, &adapter, scratch, sizeof(scratch));
    assert(fh8626_native_adapter_pump(&adapter, fake_sink) == 0);
    assert(sink_calls == 1u && fake.step_calls == 1u);
    assert(adapter.frames_delivered == 1u);
    assert(adapter.last_timestamp_raw == 0xaabbccddu);
    assert(adapter.sequence == 1u && adapter.pts_us == 40000u);

    prepare(&fake, 112u, 1);
    init_all(&fake, &life, &backend, &adapter, scratch, sizeof(scratch));
    assert(fh8626_native_adapter_pump(&adapter, fake_sink) == 0);
    assert(fake.copy_calls == 2u);
    assert(fake.step_calls == 1u);
}

static void test_sink_error_after_release(void)
{
    struct fake_kernel fake;
    struct fh8626_lifecycle life;
    struct fh8626_stream_backend backend;
    struct fh8626_native_adapter adapter;
    uint8_t scratch[64];

    prepare(&fake, 8u, 0);
    init_all(&fake, &life, &backend, &adapter, scratch, sizeof(scratch));
    sink_result = -ECANCELED;
    assert(fh8626_native_adapter_pump(&adapter, fake_sink) == -ECANCELED);
    assert(fake.step_calls == 1u);
    assert(fh8626_stream_backend_balanced(&backend));
    assert(adapter.sink_errors == 1u);
}

static void test_copy_and_format_errors_release(void)
{
    struct fake_kernel fake;
    struct fh8626_lifecycle life;
    struct fh8626_stream_backend backend;
    struct fh8626_native_adapter adapter;
    uint8_t scratch[64];

    prepare(&fake, 8u, 0);
    init_all(&fake, &life, &backend, &adapter, scratch, sizeof(scratch));
    fake.copy_ret = -EFAULT;
    assert(fh8626_native_adapter_pump(&adapter, fake_sink) == -EFAULT);
    assert(fake.step_calls == 1u && sink_calls == 0u);
    assert(adapter.copy_errors == 1u);

    prepare(&fake, 8u, 0);
    memset(fake.ring + 8u, 0x55, expected_len);
    init_all(&fake, &life, &backend, &adapter, scratch, sizeof(scratch));
    assert(fh8626_native_adapter_pump(&adapter, fake_sink) == -EBADMSG);
    assert(fake.step_calls == 1u && sink_calls == 0u);
    assert(adapter.malformed_frames == 1u);
}

static void test_capacity_and_release_failure(void)
{
    struct fake_kernel fake;
    struct fh8626_lifecycle life;
    struct fh8626_stream_backend backend;
    struct fh8626_native_adapter adapter;
    uint8_t small[8];
    uint8_t scratch[64];

    prepare(&fake, 8u, 0);
    init_all(&fake, &life, &backend, &adapter, small, sizeof(small));
    assert(fh8626_native_adapter_pump(&adapter, fake_sink) == -EMSGSIZE);
    assert(fake.step_calls == 1u && sink_calls == 0u);

    prepare(&fake, 8u, 0);
    init_all(&fake, &life, &backend, &adapter, scratch, sizeof(scratch));
    fake.step_ret = -EIO;
    assert(fh8626_native_adapter_pump(&adapter, fake_sink) == -EIO);
    assert(sink_calls == 0u);
    assert(!fh8626_stream_backend_balanced(&backend));
    fake.step_ret = 0;
    assert(fh8626_stream_backend_release(&backend) == 0);
    assert(fh8626_stream_backend_balanced(&backend));
}

int main(void)
{
    test_contiguous_and_wrap();
    test_sink_error_after_release();
    test_copy_and_format_errors_release();
    test_capacity_and_release_failure();
    puts("fh8626_native_adapter PASS");
    return 0;
}
