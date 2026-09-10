#include "fh8626_stream_backend.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

struct fake_kernel {
    uint32_t desc[FH8626_MEDIA_STREAM_DESC_WORDS];
    int media_ret;
    int step_ret;
    unsigned media_calls;
    unsigned step_calls;
};

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

static void life_to_streaming(struct fh8626_lifecycle *life)
{
    fh8626_lifecycle_reset(life);
    assert(fh8626_lifecycle_apply(life, FH8626_LIFE_HAL_INIT) == 0);
    assert(fh8626_lifecycle_apply(life, FH8626_LIFE_SYSTEM_INIT) == 0);
    assert(fh8626_lifecycle_apply(life, FH8626_LIFE_PIPELINE_CREATE) == 0);
    assert(fh8626_lifecycle_apply(life, FH8626_LIFE_VIDEO_CREATE) == 0);
    assert(fh8626_lifecycle_apply(life, FH8626_LIFE_STREAM_START) == 0);
}

static void valid_desc(struct fake_kernel *fake, uint32_t virt, uint32_t len)
{
    memset(fake, 0, sizeof(*fake));
    fake->desc[1] = FH8626_MEDIA_STREAM_KIND;
    fake->desc[6] = 0x22000000u;
    fake->desc[7] = virt;
    fake->desc[8] = len;
    fake->desc[10] = 0x11223344u;
}

static void test_contiguous_and_release(void)
{
    struct fake_kernel fake;
    struct fh8626_lifecycle life;
    struct fh8626_stream_backend backend;
    struct fh8626_native_stream stream;

    valid_desc(&fake, 0x10001000u, 0x800u);
    life_to_streaming(&life);
    assert(fh8626_stream_backend_init(&backend, 4, 5, 0x10000000u, 0x2000u,
        fake_ioctl, &fake, &life) == 0);
    assert(fh8626_stream_backend_acquire(&backend, &stream) == 0);
    assert(stream.first == (const uint8_t *)(uintptr_t)0x10001000u);
    assert(stream.first_len == 0x800u);
    assert(stream.second == NULL);
    assert(stream.second_len == 0u);
    assert(stream.phys == 0x22000000u);
    assert(stream.timestamp_raw == 0x11223344u);
    assert(fake.media_calls == 1u && fake.step_calls == 0u);
    assert(fh8626_stream_backend_acquire(&backend, &stream) == -EBUSY);
    assert(fh8626_stream_backend_release(&backend) == 0);
    assert(fake.step_calls == 1u);
    assert(fh8626_stream_backend_balanced(&backend));
    assert(fh8626_stream_backend_release(&backend) == -EPERM);
}

static void test_wrap(void)
{
    struct fake_kernel fake;
    struct fh8626_stream_backend backend;
    struct fh8626_native_stream stream;

    valid_desc(&fake, 0x10001c00u, 0x800u);
    assert(fh8626_stream_backend_init(&backend, 4, 5, 0x10000000u, 0x2000u,
        fake_ioctl, &fake, NULL) == 0);
    assert(fh8626_stream_backend_acquire(&backend, &stream) == 0);
    assert(stream.first_len == 0x400u);
    assert(stream.second == (const uint8_t *)(uintptr_t)0x10000000u);
    assert(stream.second_len == 0x400u);
    assert(fh8626_stream_backend_release(&backend) == 0);
    assert(fake.step_calls == 1u);
}

static void test_no_frame_and_media_error(void)
{
    struct fake_kernel fake;
    struct fh8626_stream_backend backend;
    struct fh8626_native_stream stream;

    memset(&fake, 0, sizeof(fake));
    assert(fh8626_stream_backend_init(&backend, 4, 5, 0x10000000u, 0x2000u,
        fake_ioctl, &fake, NULL) == 0);
    assert(fh8626_stream_backend_acquire(&backend, &stream) == -EAGAIN);
    assert(fake.step_calls == 0u);

    fake.media_ret = -EIO;
    assert(fh8626_stream_backend_acquire(&backend, &stream) == -EIO);
    assert(fake.step_calls == 0u);
}

static void test_bad_descriptor_released(void)
{
    struct fake_kernel fake;
    struct fh8626_stream_backend backend;
    struct fh8626_native_stream stream;

    valid_desc(&fake, 0x10001000u, 0x3000u);
    assert(fh8626_stream_backend_init(&backend, 4, 5, 0x10000000u, 0x2000u,
        fake_ioctl, &fake, NULL) == 0);
    assert(fh8626_stream_backend_acquire(&backend, &stream) == -ERANGE);
    assert(fake.step_calls == 1u);
    assert(fh8626_stream_backend_balanced(&backend));
}

static void test_release_retry(void)
{
    struct fake_kernel fake;
    struct fh8626_stream_backend backend;
    struct fh8626_native_stream stream;

    valid_desc(&fake, 0x10001000u, 0x800u);
    assert(fh8626_stream_backend_init(&backend, 4, 5, 0x10000000u, 0x2000u,
        fake_ioctl, &fake, NULL) == 0);
    assert(fh8626_stream_backend_acquire(&backend, &stream) == 0);
    fake.step_ret = -EIO;
    assert(fh8626_stream_backend_release(&backend) == -EIO);
    assert(!fh8626_stream_backend_balanced(&backend));
    fake.step_ret = 0;
    assert(fh8626_stream_backend_release(&backend) == 0);
    assert(fake.step_calls == 2u);
    assert(fh8626_stream_backend_balanced(&backend));
}

static void test_lifecycle_precheck(void)
{
    struct fake_kernel fake;
    struct fh8626_lifecycle life;
    struct fh8626_stream_backend backend;
    struct fh8626_native_stream stream;

    valid_desc(&fake, 0x10001000u, 0x800u);
    fh8626_lifecycle_reset(&life);
    assert(fh8626_stream_backend_init(&backend, 4, 5, 0x10000000u, 0x2000u,
        fake_ioctl, &fake, &life) == 0);
    assert(fh8626_stream_backend_acquire(&backend, &stream) == -EPERM);
    assert(fake.media_calls == 0u);
}

int main(void)
{
    test_contiguous_and_release();
    test_wrap();
    test_no_frame_and_media_error();
    test_bad_descriptor_released();
    test_release_retry();
    test_lifecycle_precheck();
    puts("fh8626_stream_backend PASS");
    return 0;
}
