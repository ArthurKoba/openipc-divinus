#include "fh8626_native_runtime.h"

#include <errno.h>
#include <string.h>

struct runtime_stage {
    fh8626_runtime_call up;
    fh8626_lifecycle_event up_event;
    fh8626_runtime_call down;
    fh8626_lifecycle_event down_event;
};

static int ops_complete(const struct fh8626_native_runtime_ops *ops)
{
    return ops && ops->hal_init && ops->system_init && ops->pipeline_create &&
        ops->video_create && ops->stream_start && ops->stream_stop &&
        ops->video_destroy && ops->pipeline_destroy && ops->system_deinit &&
        ops->hal_deinit;
}

static struct runtime_stage stage_at(struct fh8626_native_runtime *runtime,
    unsigned index)
{
    struct runtime_stage stage;

    memset(&stage, 0, sizeof(stage));
    switch (index) {
        case 0:
            stage.up = runtime->ops.hal_init;
            stage.up_event = FH8626_LIFE_HAL_INIT;
            stage.down = runtime->ops.hal_deinit;
            stage.down_event = FH8626_LIFE_HAL_DEINIT;
            break;
        case 1:
            stage.up = runtime->ops.system_init;
            stage.up_event = FH8626_LIFE_SYSTEM_INIT;
            stage.down = runtime->ops.system_deinit;
            stage.down_event = FH8626_LIFE_SYSTEM_DEINIT;
            break;
        case 2:
            stage.up = runtime->ops.pipeline_create;
            stage.up_event = FH8626_LIFE_PIPELINE_CREATE;
            stage.down = runtime->ops.pipeline_destroy;
            stage.down_event = FH8626_LIFE_PIPELINE_DESTROY;
            break;
        case 3:
            stage.up = runtime->ops.video_create;
            stage.up_event = FH8626_LIFE_VIDEO_CREATE;
            stage.down = runtime->ops.video_destroy;
            stage.down_event = FH8626_LIFE_VIDEO_DESTROY;
            break;
        default:
            stage.up = runtime->ops.stream_start;
            stage.up_event = FH8626_LIFE_STREAM_START;
            stage.down = runtime->ops.stream_stop;
            stage.down_event = FH8626_LIFE_STREAM_STOP;
            break;
    }
    return stage;
}

static int unwind(struct fh8626_native_runtime *runtime, unsigned completed)
{
    int first_error = 0;

    while (completed) {
        struct runtime_stage stage = stage_at(runtime, completed - 1u);
        int ret = stage.down(runtime->opaque);
        if (ret) {
            if (!first_error)
                first_error = ret;
            break;
        }
        ret = fh8626_lifecycle_apply(&runtime->life, stage.down_event);
        if (ret) {
            if (!first_error)
                first_error = ret;
            break;
        }
        completed--;
    }
    return first_error;
}

int fh8626_native_runtime_init(struct fh8626_native_runtime *runtime,
    const struct fh8626_native_runtime_ops *ops, void *opaque)
{
    if (!runtime || !ops_complete(ops))
        return -EINVAL;

    memset(runtime, 0, sizeof(*runtime));
    runtime->ops = *ops;
    runtime->opaque = opaque;
    fh8626_lifecycle_reset(&runtime->life);
    return 0;
}

int fh8626_native_runtime_start(struct fh8626_native_runtime *runtime)
{
    unsigned completed = 0;

    if (!runtime)
        return -EINVAL;
    if (runtime->life.state != FH8626_LIFE_COLD)
        return -EALREADY;

    for (unsigned i = 0; i < 5u; i++) {
        struct runtime_stage stage = stage_at(runtime, i);
        int ret = stage.up(runtime->opaque);
        if (!ret)
            ret = fh8626_lifecycle_apply(&runtime->life, stage.up_event);
        if (ret) {
            runtime->start_failures++;
            if (!unwind(runtime, completed) && runtime->life.state == FH8626_LIFE_COLD)
                return ret;
            return -EUCLEAN;
        }
        completed++;
    }

    runtime->starts++;
    return 0;
}

static unsigned completed_for_state(fh8626_lifecycle_state state)
{
    switch (state) {
        case FH8626_LIFE_COLD: return 0u;
        case FH8626_LIFE_HAL_READY: return 1u;
        case FH8626_LIFE_SYSTEM_READY: return 2u;
        case FH8626_LIFE_PIPELINE_READY: return 3u;
        case FH8626_LIFE_VIDEO_READY: return 4u;
        case FH8626_LIFE_STREAMING: return 5u;
    }
    return 0u;
}

int fh8626_native_runtime_stop(struct fh8626_native_runtime *runtime)
{
    unsigned completed;
    int ret;

    if (!runtime)
        return -EINVAL;
    if (runtime->life.state == FH8626_LIFE_COLD)
        return 0;
    if (!fh8626_lifecycle_balanced(&runtime->life))
        return -EBUSY;

    completed = completed_for_state(runtime->life.state);
    if (!completed)
        return -EPERM;

    ret = unwind(runtime, completed);
    if (ret) {
        runtime->stop_failures++;
        return ret;
    }
    runtime->stops++;
    return 0;
}

int fh8626_native_runtime_ready(const struct fh8626_native_runtime *runtime)
{
    return runtime && runtime->life.state == FH8626_LIFE_STREAMING &&
        fh8626_lifecycle_balanced(&runtime->life);
}
