#pragma once

#include "fh8626_native_adapter.h"
#include "fh8626_native_runtime.h"
#include "fh8626_stream_backend.h"
#include "native/isp/fh8626_isp_runtime.h"
#include "native/sensor/gc1054/fh8626_sensor_gc1054.h"

struct fh8626_kernel;

/* Generic HAL configuration. Board-specific lens/PTZ/GPIO policy stays in
 * Builder and does not cross this boundary. */
struct fh8626_native_config {
    uint32_t width;
    uint32_t height;
    uint32_t fps;
    uint32_t gop;
    uint32_t profile;
    uint32_t mirror;
    uint32_t flip;
    uint32_t rc_mode;
    uint32_t bitrate_kbps;
};

int fh8626_kernel_start(struct fh8626_kernel **out,
    const struct fh8626_native_config *config, fh8626_video_sink sink);
int fh8626_kernel_stop(struct fh8626_kernel *kernel);
int fh8626_kernel_is_running(const struct fh8626_kernel *kernel);
int fh8626_kernel_request_idr(struct fh8626_kernel *kernel);
int fh8626_kernel_set_bitrate(struct fh8626_kernel *kernel,
    uint32_t bitrate_kbps);
int fh8626_kernel_set_mirror_flip(struct fh8626_kernel *kernel,
    int mirror, int flip);

/* JPEG/MJPEG media-object lifecycle.  The source is a dedicated VPU channel
 * bound to the kernel JPEG stream object; callers never submit raw VPU
 * addresses directly to /dev/jpeg. */
int fh8626_kernel_jpeg_init(struct fh8626_kernel *kernel, uint32_t mode,
    uint32_t width, uint32_t height, uint32_t quality, uint32_t fps,
    uint32_t bitrate, uint32_t rc_mode);
int fh8626_kernel_jpeg_deinit_mode(struct fh8626_kernel *kernel, uint32_t mode);
int fh8626_kernel_jpeg_deinit(struct fh8626_kernel *kernel);
int fh8626_kernel_jpeg_get(struct fh8626_kernel *kernel, uint32_t width,
    uint32_t height, uint32_t quality, hal_jpegdata *jpeg);
