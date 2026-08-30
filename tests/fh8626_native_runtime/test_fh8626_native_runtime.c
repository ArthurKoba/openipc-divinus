#include "fh8626_native_adapter.h"
#include "fh8626_native_runtime.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#define RING_BASE 0x10000000u
#define RING_SIZE 128u

struct fake_world {
    char log[64];
    unsigned log_len;
    int fail_up;
    int fail_down;
    uint8_t ring[RING_SIZE];
    uint32_t desc[FH8626_MEDIA_STREAM_DESC_WORDS];
    unsigned media_calls;
    unsigned step_calls;
};

static void mark(struct fake_world *world, char c)
{
    assert(world->log_len + 1u < sizeof(world->log));
    world->log[world->log_len++] = c;
    world->log[world->log_len] = '\0';
}

static int up_stage(void *opaque, int stage, char mark_char)
{
    struct fake_world *world = opaque;
    mark(world, mark_char);
    return world->fail_up == stage ? -EIO : 0;
}

static int down_stage(void *opaque, int stage, char mark_char)
{
    struct fake_world *world = opaque;
    mark(world, mark_char);
    return world->fail_down == stage ? -EIO : 0;
}

static int up_h(void *o) { return up_stage(o, 1, 'H'); }
static int up_s(void *o) { return up_stage(o, 2, 'S'); }
static int up_p(void *o) { return up_stage(o, 3, 'P'); }
static int up_v(void *o) { return up_stage(o, 4, 'V'); }
static int up_t(void *o) { return up_stage(o, 5, 'T'); }
static int down_t(void *o) { return down_stage(o, 5, 't'); }
static int down_v(void *o) { return down_stage(o, 4, 'v'); }
static int down_p(void *o) { return down_stage(o, 3, 'p'); }
static int down_s(void *o) { return down_stage(o, 2, 's'); }
static int down_h(void *o) { return down_stage(o, 1, 'h'); }

static struct fh8626_native_runtime_ops fake_ops(void)
{
    struct fh8626_native_runtime_ops ops;
    ops.hal_init = up_h;
    ops.system_init = up_s;
    ops.pipeline_create = up_p;
    ops.video_create = up_v;
    ops.stream_start = up_t;
    ops.stream_stop = down_t;
    ops.video_destroy = down_v;
    ops.pipeline_destroy = down_p;
    ops.system_deinit = down_s;
    ops.hal_deinit = down_h;
    return ops;
}

static int fake_ioctl(void *opaque, int fd, unsigned long request, void *arg)
{
    struct fake_world *world = opaque;
    (void)fd;
    if (request == FH8626_MEDIA_STREAM_6) {
        world->media_calls++;
        memcpy(arg, world->desc, sizeof(world->desc));
        return 0;
    }
    if (request == FH8626_PAE_STREAM_STEP) {
        uint32_t *channel = arg;
        assert(*channel == FH8626_NATIVE_CHANNEL);
        world->step_calls++;
        return 0;
    }
    return -ENOTTY;
}

static int fake_copy(void *opaque, uint8_t *dst, const uint8_t *src, size_t len)
{
    struct fake_world *world = opaque;
    size_t offset = (size_t)((uintptr_t)src - RING_BASE);
    assert(offset + len <= RING_SIZE);
    memcpy(dst, world->ring + offset, len);
    return 0;
}

static int fake_sink(char index, hal_vidstream *stream)
{
    assert(index == FH8626_NATIVE_CHANNEL);
    assert(stream && stream->count == 1u);
    assert(stream->pack && stream->pack->naluCnt == 3);
    return 0;
}

static void put_frame(struct fake_world *world)
{
    static const uint8_t frame[] = {
        0x00,0x00,0x00,0x01,0x67,0x42,0x00,0x1f,
        0x00,0x00,0x00,0x01,0x68,0xce,0x06,0xe2,
        0x00,0x00,0x00,0x01,0x65,0x88,0x84,0x21
    };
    memset(world->ring, 0, sizeof(world->ring));
    memset(world->desc, 0, sizeof(world->desc));
    memcpy(world->ring + 16u, frame, sizeof(frame));
    world->desc[1] = FH8626_MEDIA_STREAM_KIND;
    world->desc[6] = 0x22000000u;
    world->desc[7] = RING_BASE + 16u;
    world->desc[8] = sizeof(frame);
}

static void test_full_stub_pipeline(void)
{
    struct fake_world world;
    struct fh8626_native_runtime runtime;
    struct fh8626_native_runtime_ops ops = fake_ops();
    struct fh8626_stream_backend backend;
    struct fh8626_native_adapter adapter;
    uint8_t scratch[64];

    memset(&world, 0, sizeof(world));
    assert(fh8626_native_runtime_init(&runtime, &ops, &world) == 0);
    assert(fh8626_native_runtime_start(&runtime) == 0);
    assert(!strcmp(world.log, "HSPVT"));
    assert(fh8626_native_runtime_ready(&runtime));

    put_frame(&world);
    assert(fh8626_stream_backend_init(&backend, 4, 5, RING_BASE, RING_SIZE,
        fake_ioctl, &world, &runtime.life) == 0);
    assert(fh8626_native_adapter_init(&adapter, &backend, scratch, sizeof(scratch),
        fake_copy, &world, 40000u) == 0);
    assert(fh8626_native_adapter_pump(&adapter, fake_sink) == 0);
    assert(world.media_calls == 1u && world.step_calls == 1u);
    assert(adapter.frames_delivered == 1u);
    assert(fh8626_native_runtime_ready(&runtime));

    assert(fh8626_native_runtime_stop(&runtime) == 0);
    assert(!strcmp(world.log, "HSPVTtvpsh"));
    assert(runtime.life.state == FH8626_LIFE_COLD);
    assert(runtime.starts == 1u && runtime.stops == 1u);
}

static void test_start_failure_rolls_back(void)
{
    struct fake_world world;
    struct fh8626_native_runtime runtime;
    struct fh8626_native_runtime_ops ops = fake_ops();

    memset(&world, 0, sizeof(world));
    world.fail_up = 4;
    assert(fh8626_native_runtime_init(&runtime, &ops, &world) == 0);
    assert(fh8626_native_runtime_start(&runtime) == -EIO);
    assert(!strcmp(world.log, "HSPVpsh"));
    assert(runtime.life.state == FH8626_LIFE_COLD);
    assert(runtime.start_failures == 1u);
}

static void test_lease_blocks_stop(void)
{
    struct fake_world world;
    struct fh8626_native_runtime runtime;
    struct fh8626_native_runtime_ops ops = fake_ops();

    memset(&world, 0, sizeof(world));
    assert(fh8626_native_runtime_init(&runtime, &ops, &world) == 0);
    assert(fh8626_native_runtime_start(&runtime) == 0);
    assert(fh8626_lifecycle_lease_begin(&runtime.life) == 0);
    assert(fh8626_native_runtime_stop(&runtime) == -EBUSY);
    assert(fh8626_lifecycle_lease_release(&runtime.life) == 0);
    assert(fh8626_native_runtime_stop(&runtime) == 0);
}

static void test_stop_failure_is_retryable(void)
{
    struct fake_world world;
    struct fh8626_native_runtime runtime;
    struct fh8626_native_runtime_ops ops = fake_ops();

    memset(&world, 0, sizeof(world));
    assert(fh8626_native_runtime_init(&runtime, &ops, &world) == 0);
    assert(fh8626_native_runtime_start(&runtime) == 0);
    world.fail_down = 3;
    assert(fh8626_native_runtime_stop(&runtime) == -EIO);
    assert(runtime.life.state == FH8626_LIFE_PIPELINE_READY);
    world.fail_down = 0;
    assert(fh8626_native_runtime_stop(&runtime) == 0);
    assert(runtime.life.state == FH8626_LIFE_COLD);
    assert(runtime.stop_failures == 1u && runtime.stops == 1u);
}

int main(void)
{
    test_full_stub_pipeline();
    test_start_failure_rolls_back();
    test_lease_blocks_stop();
    test_stop_failure_is_retryable();
    puts("fh8626_native_runtime PASS");
    return 0;
}
