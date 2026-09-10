#pragma once

#include "fh8626_contract.h"

#include <stddef.h>

typedef int (*fh8626_runtime_call)(void *opaque);

struct fh8626_native_runtime_ops {
    fh8626_runtime_call hal_init;
    fh8626_runtime_call system_init;
    fh8626_runtime_call pipeline_create;
    fh8626_runtime_call video_create;
    fh8626_runtime_call stream_start;
    fh8626_runtime_call stream_stop;
    fh8626_runtime_call video_destroy;
    fh8626_runtime_call pipeline_destroy;
    fh8626_runtime_call system_deinit;
    fh8626_runtime_call hal_deinit;
};

struct fh8626_native_runtime {
    struct fh8626_lifecycle life;
    struct fh8626_native_runtime_ops ops;
    void *opaque;
    unsigned starts;
    unsigned stops;
    unsigned start_failures;
    unsigned stop_failures;
};

int fh8626_native_runtime_init(struct fh8626_native_runtime *runtime,
    const struct fh8626_native_runtime_ops *ops, void *opaque);
int fh8626_native_runtime_start(struct fh8626_native_runtime *runtime);
int fh8626_native_runtime_stop(struct fh8626_native_runtime *runtime);
int fh8626_native_runtime_ready(const struct fh8626_native_runtime *runtime);
