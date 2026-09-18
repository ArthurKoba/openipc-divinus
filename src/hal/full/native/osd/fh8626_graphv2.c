#include "fh8626_graphv2.h"

#include <errno.h>
#include <string.h>

_Static_assert(FH8626_GRAPHV2_WIRE_WORDS * sizeof(uint32_t) == 0x448,
    "FH8626 GraphV2 ioctl wire size");
_Static_assert(FH8626_GRAPHV2_PUBLIC_WORDS ==
    17u + 2u * FH8626_GRAPHV2_PLANE_WORDS,
    "FH8626 GraphV2 public shape");

static int logo_geometry_valid(const struct fh8626_graphv2_logo *logo)
{
    uint64_t right, bottom;

    if (!logo || logo->graph_index >= FH8626_OSD_HW_SLOTS)
        return 0;
    if (!logo->width || !logo->height ||
        logo->width > 0x1000u || logo->height > 0x1000u ||
        (logo->width & 1u) || (logo->height & 1u))
        return 0;
    if (logo->x > 0xffffu || logo->y > 0xffffu ||
        logo->width > 0xffffu || logo->height > 0xffffu)
        return 0;
    right = (uint64_t)logo->x + logo->width;
    bottom = (uint64_t)logo->y + logo->height;
    if (right > 0x10000u || bottom > 0x10000u)
        return 0;
    if (logo->stride < logo->width * 2u || (logo->stride & 7u) ||
        logo->stride > 0xffffu)
        return 0;
    if (logo->opacity > 0xffu)
        return 0;
    if (logo->enable && !logo->phys)
        return 0;
    return 1;
}

int fh8626_graphv2_build_logo(const struct fh8626_graphv2_logo *logo,
    uint32_t pub[FH8626_GRAPHV2_PUBLIC_WORDS])
{
    if (!pub || !logo_geometry_valid(logo))
        return -EINVAL;

    memset(pub, 0, FH8626_GRAPHV2_PUBLIC_WORDS * sizeof(*pub));
    pub[0] = logo->enable ? 1u : 0u;
    pub[1] = logo->graph_index;
    pub[2] = 0x10u;       /* 16-bit logo pixels */
    pub[3] = logo->phys;  /* VMM physical buffer */
    if (logo->enable) {
        pub[6] = 1u;      /* constant-alpha enable */
        pub[7] = logo->opacity;
    }
    pub[8] = 2u;          /* stock logo GraphV2 mode */
    pub[12] = logo->width;
    pub[13] = logo->height;
    pub[14] = logo->x;
    pub[15] = logo->y;
    pub[16] = logo->stride;
    return 0;
}

int fh8626_graphv2_build_disable(uint32_t graph_index,
    uint32_t pub[FH8626_GRAPHV2_PUBLIC_WORDS])
{
    const struct fh8626_graphv2_logo logo = {
        .enable = 0u,
        .graph_index = graph_index,
        .phys = 0u,
        .opacity = 0u,
        .x = 0u,
        .y = 0u,
        .width = 16u,
        .height = 16u,
        .stride = 32u,
    };

    return fh8626_graphv2_build_logo(&logo, pub);
}

int fh8626_graphv2_public_to_wire(uint32_t selector,
    const uint32_t pub[FH8626_GRAPHV2_PUBLIC_WORDS],
    uint32_t wire[FH8626_GRAPHV2_WIRE_WORDS])
{
    unsigned i;

    if (!pub || !wire || selector > 2u)
        return -EINVAL;
    if ((selector == 0u && pub[1] >= FH8626_GRAPHV2_GLOBAL_SLOTS) ||
        (selector != 0u && pub[1] >= FH8626_GRAPHV2_CHANNEL_SLOTS))
        return -EINVAL;

    memset(wire, 0, FH8626_GRAPHV2_WIRE_WORDS * sizeof(*wire));
    wire[0] = selector;
    wire[1] = pub[1];
    wire[2] = pub[0];
    for (i = 2; i <= 16; ++i)
        wire[i + 1] = pub[i];
    memcpy(&wire[18], &pub[17],
        2u * FH8626_GRAPHV2_PLANE_WORDS * sizeof(uint32_t));
    return 0;
}
