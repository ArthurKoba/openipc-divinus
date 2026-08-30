#pragma once

#include "fh8626_contract.h"

#include <stdint.h>

typedef int (*fh8626_ioctl_call)(void *opaque, int fd, unsigned long request, void *arg);

struct fh8626_native_stream {
    const uint8_t *first;
    uint32_t first_len;
    const uint8_t *second;
    uint32_t second_len;
    uint32_t timestamp_raw;
    uint32_t phys;
    uint32_t length;
};

struct fh8626_stream_backend {
    int media_fd;
    int pae_fd;
    uintptr_t ring_base;
    uint32_t ring_size;
    fh8626_ioctl_call ioctl_call;
    void *ioctl_opaque;
    struct fh8626_lifecycle *life;
    int lease_held;
    uint32_t desc[FH8626_MEDIA_STREAM_DESC_WORDS];
};

int fh8626_stream_backend_init(struct fh8626_stream_backend *backend,
    int media_fd, int pae_fd, uintptr_t ring_base, uint32_t ring_size,
    fh8626_ioctl_call ioctl_call, void *ioctl_opaque,
    struct fh8626_lifecycle *life);
int fh8626_stream_backend_acquire(struct fh8626_stream_backend *backend,
    struct fh8626_native_stream *stream);
int fh8626_stream_backend_release(struct fh8626_stream_backend *backend);
int fh8626_stream_backend_balanced(const struct fh8626_stream_backend *backend);
