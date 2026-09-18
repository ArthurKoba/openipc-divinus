#pragma once

#include <stdint.h>

#define FH8626_GRAPHV2_PUBLIC_WORDS 273u
#define FH8626_GRAPHV2_WIRE_WORDS 274u
#define FH8626_GRAPHV2_PLANE_WORDS 128u
#define FH8626_OSD_HW_SLOTS 6u
#define FH8626_GRAPHV2_MAIN_SELECTOR 1u

struct fh8626_graphv2_logo {
    uint32_t enable;
    uint32_t graph_index;
    uint32_t phys;
    uint32_t opacity;
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
};

int fh8626_graphv2_build_logo(const struct fh8626_graphv2_logo *logo,
    uint32_t pub[FH8626_GRAPHV2_PUBLIC_WORDS]);
int fh8626_graphv2_build_disable(uint32_t graph_index,
    uint32_t pub[FH8626_GRAPHV2_PUBLIC_WORDS]);
int fh8626_graphv2_public_to_wire(uint32_t selector,
    const uint32_t pub[FH8626_GRAPHV2_PUBLIC_WORDS],
    uint32_t wire[FH8626_GRAPHV2_WIRE_WORDS]);
