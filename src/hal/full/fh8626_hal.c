#include "fh8626_hal.h"
#include "../globals.h"

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define FH8626_STUB_RING_BASE 0x10000000u
#define FH8626_STUB_RING_SIZE 4096u
#define FH8626_STUB_FRAME_INTERVAL_US 40000u

hal_chnstate fh8626_state[FH8626_VENC_CHN_NUM];

#ifdef FH8626_NATIVE_STUB
struct fh8626_stub_context {
    struct fh8626_native_runtime runtime;
    struct fh8626_stream_backend backend;
    struct fh8626_native_adapter adapter;
    pthread_t thread;
    pthread_mutex_t lock;
    int lock_ready;
    int running;
    fh8626_video_sink sink;
    uint8_t ring[FH8626_STUB_RING_SIZE];
    uint8_t scratch[FH8626_STUB_RING_SIZE];
    uint32_t desc[FH8626_MEDIA_STREAM_DESC_WORDS];
    unsigned media_calls;
    unsigned step_calls;
};

static struct fh8626_stub_context stub;

static int stage_ok(void *opaque)
{
    (void)opaque;
    return 0;
}

static struct fh8626_native_runtime_ops stub_runtime_ops(void)
{
    struct fh8626_native_runtime_ops ops;

    memset(&ops, 0, sizeof(ops));
    ops.hal_init = stage_ok;
    ops.system_init = stage_ok;
    ops.pipeline_create = stage_ok;
    ops.video_create = stage_ok;
    ops.stream_start = stage_ok;
    ops.stream_stop = stage_ok;
    ops.video_destroy = stage_ok;
    ops.pipeline_destroy = stage_ok;
    ops.system_deinit = stage_ok;
    ops.hal_deinit = stage_ok;
    return ops;
}

static void stub_prepare_frame(struct fh8626_stub_context *ctx)
{
    static const uint8_t frame[] = {
        0x00,0x00,0x00,0x01,0x67,0x42,0x00,0x1f,
        0x00,0x00,0x00,0x01,0x68,0xce,0x06,0xe2,
        0x00,0x00,0x00,0x01,0x65,0x88,0x84,0x21
    };
    const uint32_t offset = 32u;

    memset(ctx->ring, 0, sizeof(ctx->ring));
    memset(ctx->desc, 0, sizeof(ctx->desc));
    memcpy(ctx->ring + offset, frame, sizeof(frame));
    ctx->desc[1] = FH8626_MEDIA_STREAM_KIND;
    ctx->desc[6] = 0x22000000u;
    ctx->desc[7] = FH8626_STUB_RING_BASE + offset;
    ctx->desc[8] = (uint32_t)sizeof(frame);
}

static int stub_ioctl(void *opaque, int fd, unsigned long request, void *arg)
{
    struct fh8626_stub_context *ctx = opaque;
    (void)fd;

    if (request == FH8626_MEDIA_STREAM_6) {
        ctx->media_calls++;
        memcpy(arg, ctx->desc, sizeof(ctx->desc));
        return 0;
    }
    if (request == FH8626_PAE_STREAM_STEP) {
        uint32_t *channel = arg;
        if (!channel || *channel != FH8626_NATIVE_CHANNEL)
            return -EINVAL;
        ctx->step_calls++;
        return 0;
    }
    return -ENOTTY;
}

static int stub_copy(void *opaque, uint8_t *dst, const uint8_t *src, size_t len)
{
    struct fh8626_stub_context *ctx = opaque;
    uintptr_t logical = (uintptr_t)src;
    size_t offset;

    if (logical < FH8626_STUB_RING_BASE)
        return -ERANGE;
    offset = (size_t)(logical - FH8626_STUB_RING_BASE);
    if (offset > sizeof(ctx->ring) || len > sizeof(ctx->ring) - offset)
        return -ERANGE;
    memcpy(dst, ctx->ring + offset, len);
    return 0;
}

static int stub_running(struct fh8626_stub_context *ctx)
{
    int running;

    pthread_mutex_lock(&ctx->lock);
    running = ctx->running;
    pthread_mutex_unlock(&ctx->lock);
    return running;
}

static void *fh8626_stub_video_thread(void *opaque)
{
    struct fh8626_stub_context *ctx = opaque;

    while (keepRunning && stub_running(ctx)) {
        int ret = fh8626_native_adapter_pump(&ctx->adapter, ctx->sink);
        if (ret && ret != -EAGAIN)
            break;
        usleep(FH8626_STUB_FRAME_INTERVAL_US);
    }
    return NULL;
}
#endif

int fh8626_hal_stub_enabled(void)
{
#ifdef FH8626_NATIVE_STUB
    return 1;
#else
    return 0;
#endif
}

int fh8626_sdk_start(fh8626_video_sink sink)
{
#ifndef FH8626_NATIVE_STUB
    (void)sink;
    return -ENOTSUP;
#else
    struct fh8626_native_runtime_ops ops;
    int ret;

    if (!sink)
        return -EINVAL;
    memset(&stub, 0, sizeof(stub));
    ret = pthread_mutex_init(&stub.lock, NULL);
    if (ret)
        return -ret;
    stub.lock_ready = 1;
    stub.sink = sink;
    stub_prepare_frame(&stub);

    ops = stub_runtime_ops();
    ret = fh8626_native_runtime_init(&stub.runtime, &ops, &stub);
    if (ret)
        goto fail;
    ret = fh8626_native_runtime_start(&stub.runtime);
    if (ret)
        goto fail;
    ret = fh8626_stream_backend_init(&stub.backend, 3, 4,
        FH8626_STUB_RING_BASE, FH8626_STUB_RING_SIZE, stub_ioctl, &stub,
        &stub.runtime.life);
    if (ret)
        goto stop_runtime;
    ret = fh8626_native_adapter_init(&stub.adapter, &stub.backend,
        stub.scratch, sizeof(stub.scratch), stub_copy, &stub,
        FH8626_STUB_FRAME_INTERVAL_US);
    if (ret)
        goto stop_runtime;

    memset(fh8626_state, 0, sizeof(fh8626_state));
    fh8626_state[0].enable = 1;
    fh8626_state[0].mainLoop = 1;
    fh8626_state[0].payload = HAL_VIDCODEC_H264;

    pthread_mutex_lock(&stub.lock);
    stub.running = 1;
    pthread_mutex_unlock(&stub.lock);
    ret = pthread_create(&stub.thread, NULL, fh8626_stub_video_thread, &stub);
    if (ret) {
        pthread_mutex_lock(&stub.lock);
        stub.running = 0;
        pthread_mutex_unlock(&stub.lock);
        ret = -ret;
        goto stop_runtime;
    }
    return 0;

stop_runtime:
    (void)fh8626_native_runtime_stop(&stub.runtime);
fail:
    if (stub.lock_ready) {
        pthread_mutex_destroy(&stub.lock);
        stub.lock_ready = 0;
    }
    return ret;
#endif
}

int fh8626_sdk_stop(void)
{
#ifndef FH8626_NATIVE_STUB
    return -ENOTSUP;
#else
    int ret;

    if (!stub.lock_ready)
        return 0;
    pthread_mutex_lock(&stub.lock);
    stub.running = 0;
    pthread_mutex_unlock(&stub.lock);
    pthread_join(stub.thread, NULL);

    ret = fh8626_native_runtime_stop(&stub.runtime);
    pthread_mutex_destroy(&stub.lock);
    stub.lock_ready = 0;
    memset(fh8626_state, 0, sizeof(fh8626_state));
    return ret;
#endif
}
