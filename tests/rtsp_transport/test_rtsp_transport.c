#include "../../src/rtsp/tcp_input.h"
#include "../../src/rtsp/rtp_au.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void append(struct rtsp_tcp_input *input, const void *data, size_t size)
{
    assert(size <= sizeof(input->data) - input->used);
    memcpy(input->data + input->used, data, size);
    input->used += (unsigned)size;
}

static void test_rtsp_request_framing(void)
{
    static const char first[] =
        "SET_PARAMETER rtsp://camera/ RTSP/1.0\r\n"
        "CSeq: 4\r\nContent-Length: 4\r\n\r\nAB";
    static const char second[] = "CD";
    struct rtsp_tcp_input input = {0};
    char out[8192];
    unsigned length = 0;

    append(&input, first, sizeof(first) - 1u);
    assert(rtsp_tcp_extract(&input, out, &length) == 0);
    append(&input, second, sizeof(second) - 1u);
    assert(rtsp_tcp_extract(&input, out, &length) == 1);
    assert(length == sizeof(first) + sizeof(second) - 2u);
    assert(!memcmp(out + length - 4u, "ABCD", 4u));
    assert(input.used == 0u);
}

static void test_interleaved_rtcp_skip(void)
{
    static const unsigned char interleaved[] = {
        '$', 1, 0, 5, 1, 2, 3, 4, 5
    };
    static const char request[] =
        "OPTIONS rtsp://camera/ RTSP/1.0\r\nCSeq: 5\r\n\r\n";
    struct rtsp_tcp_input input = {0};
    char out[8192];
    unsigned length = 0;

    append(&input, interleaved, sizeof(interleaved));
    append(&input, request, sizeof(request) - 1u);
    assert(rtsp_tcp_extract(&input, out, &length) == 1);
    assert(length == sizeof(request) - 1u);
    assert(!memcmp(out, request, length));
    assert(input.used == 0u);
}

static void test_invalid_content_length(void)
{
    static const char request[] =
        "SET_PARAMETER rtsp://camera/ RTSP/1.0\r\n"
        "Content-Length: 9000\r\n\r\n";
    struct rtsp_tcp_input input = {0};
    char out[8192];
    unsigned length = 0;

    append(&input, request, sizeof(request) - 1u);
    assert(rtsp_tcp_extract(&input, out, &length) == -1);
}

static void test_access_unit_tail(void)
{
    unsigned char first[] = {
        0,0,0,1,0x67,0x11,
        0,0,0,1,0x68,0x22,
        0,0,0,1,0x65,0x33
    };
    unsigned char second[] = {0x41,0x44,0x55};
    hal_vidpack packs[2];
    hal_vidstream stream;
    struct rtp_au_tail tail;

    memset(packs, 0, sizeof(packs));
    packs[0].data = first;
    packs[0].length = sizeof(first);
    stream.pack = packs;
    stream.count = 1;
    assert(rtp_au_find_tail(&stream, &tail) == 0);
    assert(tail.pack == 0u);
    assert(tail.nal == first + 16u);
    assert(*tail.nal == 0x65);

    packs[1].data = second;
    packs[1].length = sizeof(second);
    stream.count = 2;
    assert(rtp_au_find_tail(&stream, &tail) == 0);
    assert(tail.pack == 1u);
    assert(tail.nal == second);

    packs[1].offset = sizeof(second) + 1u;
    assert(rtp_au_find_tail(&stream, &tail) == -1);
}

int main(void)
{
    test_rtsp_request_framing();
    test_interleaved_rtcp_skip();
    test_invalid_content_length();
    test_access_unit_tail();
    puts("rtsp_transport PASS");
    return 0;
}
