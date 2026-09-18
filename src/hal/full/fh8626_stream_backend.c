#include "fh8626_stream_backend.h"

#include <errno.h>
#include <string.h>

static int fh8626_stream_release_raw(struct fh8626_stream_backend *backend)
{
    uint32_t channel = FH8626_NATIVE_CHANNEL;

    return backend->ioctl_call(backend->ioctl_opaque, backend->pae_fd,
        FH8626_PAE_STREAM_STEP, &channel);
}

int fh8626_stream_backend_init(struct fh8626_stream_backend *backend,
    int media_fd, int pae_fd, uintptr_t ring_base, uint32_t ring_size,
    fh8626_ioctl_call ioctl_call, void *ioctl_opaque,
    struct fh8626_lifecycle *life)
{
    if (!backend || media_fd < 0 || pae_fd < 0 || !ring_base || !ring_size || !ioctl_call)
        return -EINVAL;

    memset(backend, 0, sizeof(*backend));
    backend->media_fd = media_fd;
    backend->pae_fd = pae_fd;
    backend->ring_base = ring_base;
    backend->ring_size = ring_size;
    backend->ioctl_call = ioctl_call;
    backend->ioctl_opaque = ioctl_opaque;
    backend->life = life;
    return 0;
}

int fh8626_stream_backend_acquire(struct fh8626_stream_backend *backend,
    struct fh8626_native_stream *stream)
{
    struct fh8626_stream_span span;
    int ret;

    if (!backend || !stream || !backend->ioctl_call)
        return -EINVAL;
    if (backend->lease_held)
        return -EBUSY;
    if (backend->life && backend->life->state != FH8626_LIFE_STREAMING)
        return -EPERM;

    memset(stream, 0, sizeof(*stream));
    memset(backend->desc, 0, sizeof(backend->desc));
    backend->desc[0] = FH8626_MEDIA_STREAM_KIND;

    ret = backend->ioctl_call(backend->ioctl_opaque, backend->media_fd,
        FH8626_MEDIA_STREAM_6, backend->desc);
    if (ret)
        return ret;
    if (backend->desc[1] != FH8626_MEDIA_STREAM_KIND)
        return -EAGAIN;

    ret = fh8626_stream_decode(backend->desc, backend->ring_base,
        backend->ring_size, &span);
    if (ret) {
        int release_ret = fh8626_stream_release_raw(backend);
        return release_ret ? release_ret : ret;
    }

    if (backend->life) {
        ret = fh8626_lifecycle_lease_begin(backend->life);
        if (ret) {
            int release_ret = fh8626_stream_release_raw(backend);
            return release_ret ? release_ret : ret;
        }
    }

    backend->lease_held = 1;
    stream->first = (const uint8_t *)span.first;
    stream->first_len = span.first_len;
    stream->second = (const uint8_t *)span.second;
    stream->second_len = span.second_len;
    stream->timestamp_raw = span.timestamp_raw;
    stream->phys = backend->desc[6];
    stream->length = backend->desc[8];
    return 0;
}

int fh8626_stream_backend_release(struct fh8626_stream_backend *backend)
{
    int ret;

    if (!backend || !backend->ioctl_call)
        return -EINVAL;
    if (!backend->lease_held)
        return -EPERM;

    ret = fh8626_stream_release_raw(backend);
    if (ret)
        return ret;

    backend->lease_held = 0;
    if (backend->life)
        return fh8626_lifecycle_lease_release(backend->life);
    return 0;
}

int fh8626_stream_backend_balanced(const struct fh8626_stream_backend *backend)
{
    if (!backend || backend->lease_held)
        return 0;
    if (backend->life)
        return fh8626_lifecycle_balanced(backend->life);
    return 1;
}
