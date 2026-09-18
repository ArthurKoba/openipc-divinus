#include "fh8626_graphv2.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>

static void test_logo_header_and_wire(void)
{
    struct fh8626_graphv2_logo logo = {
        .enable = 1,
        .graph_index = 5,
        .phys = 0x23456000u,
        .opacity = 200,
        .x = 16,
        .y = 32,
        .width = 320,
        .height = 64,
        .stride = 640,
    };
    uint32_t pub[FH8626_GRAPHV2_PUBLIC_WORDS];
    uint32_t wire[FH8626_GRAPHV2_WIRE_WORDS];

    assert(fh8626_graphv2_build_logo(&logo, pub) == 0);
    assert(pub[0] == 1u);
    assert(pub[1] == 5u);
    assert(pub[2] == 0x10u);
    assert(pub[3] == 0x23456000u);
    assert(pub[6] == 1u);
    assert(pub[7] == 200u);
    assert(pub[8] == 2u);
    assert(pub[12] == 320u);
    assert(pub[13] == 64u);
    assert(pub[14] == 16u);
    assert(pub[15] == 32u);
    assert(pub[16] == 640u);

    assert(fh8626_graphv2_public_to_wire(1u, pub, wire) == 0);
    assert(wire[0] == 1u);
    assert(wire[1] == 5u);
    assert(wire[2] == 1u);
    assert(wire[3] == 0x10u);
    assert(wire[4] == 0x23456000u);
    assert(wire[7] == 1u);
    assert(wire[8] == 200u);
    assert(wire[9] == 2u);
    assert(wire[13] == 320u);
    assert(wire[14] == 64u);
    assert(wire[15] == 16u);
    assert(wire[16] == 32u);
    assert(wire[17] == 640u);
}

static void test_disable_shape(void)
{
    uint32_t pub[FH8626_GRAPHV2_PUBLIC_WORDS];

    assert(fh8626_graphv2_build_disable(0u, pub) == 0);
    assert(pub[0] == 0u);
    assert(pub[1] == 0u);
    assert(pub[2] == 0x10u);
    assert(pub[3] == 0u);
    assert(pub[6] == 0u);
    assert(pub[8] == 2u);
    assert(pub[12] == 16u);
    assert(pub[13] == 16u);
    assert(pub[16] == 32u);
}

static void test_rejects_unproved_shapes(void)
{
    struct fh8626_graphv2_logo logo = {
        .enable = 1,
        .graph_index = 0,
        .phys = 0x1000u,
        .opacity = 255,
        .width = 320,
        .height = 64,
        .stride = 640,
    };
    uint32_t pub[FH8626_GRAPHV2_PUBLIC_WORDS];
    uint32_t wire[FH8626_GRAPHV2_WIRE_WORDS];

    logo.graph_index = FH8626_OSD_HW_SLOTS;
    assert(fh8626_graphv2_build_logo(&logo, pub) == -EINVAL);
    logo.graph_index = 0;

    logo.width = 319;
    assert(fh8626_graphv2_build_logo(&logo, pub) == -EINVAL);
    logo.width = 320;

    logo.stride = 639;
    assert(fh8626_graphv2_build_logo(&logo, pub) == -EINVAL);
    logo.stride = 640;

    logo.opacity = 256;
    assert(fh8626_graphv2_build_logo(&logo, pub) == -EINVAL);
    logo.opacity = 255;

    assert(fh8626_graphv2_build_logo(&logo, pub) == 0);
    assert(fh8626_graphv2_public_to_wire(3u, pub, wire) == -EINVAL);
}

int main(void)
{
    test_logo_header_and_wire();
    test_disable_shape();
    test_rejects_unproved_shapes();
    return 0;
}
