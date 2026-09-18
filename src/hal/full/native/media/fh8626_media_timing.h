#ifndef FH8626_MEDIA_TIMING_H
#define FH8626_MEDIA_TIMING_H

#include <stdint.h>

/* FH wire order is denominator in high16, numerator in low16. */
#define FH8626_OWNER_FPS_NUM 25u
#define FH8626_OWNER_FPS_DEN 1u
#define FH8626_OWNER_FPS_PACKED ((FH8626_OWNER_FPS_DEN << 16) | FH8626_OWNER_FPS_NUM)

static inline uint32_t fh8626_fps_packed(uint32_t fps)
{
    return (1u << 16) | (fps & 0xffffu);
}

static inline uint32_t fh8626_frame_interval_us(uint32_t fps)
{
    return fps ? 1000000u / fps : 0u;
}

#endif
