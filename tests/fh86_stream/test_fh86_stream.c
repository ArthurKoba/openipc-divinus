#include "../../src/source/fh86_stream.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

struct memory_reader {
    const uint8_t *data;
    size_t len;
    size_t offset;
    size_t max_chunk;
};

static void put_be32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static void put_be64(uint8_t *p, uint64_t value) {
    put_be32(p, (uint32_t)(value >> 32));
    put_be32(p + 4, (uint32_t)value);
}

static void append_frame(uint8_t *buffer, size_t *offset,
    const uint8_t *payload, uint32_t payload_len,
    uint64_t pts, uint64_t generation, uint32_t flags) {
    uint8_t *header = buffer + *offset;

    memcpy(header, "FH86", 4);
    put_be32(header + 4, FH86_WIRE_VERSION);
    put_be32(header + 8, payload_len);
    put_be32(header + 12, flags);
    put_be64(header + 16, pts);
    put_be64(header + 24, generation);

    *offset += FH86_WIRE_HEADER_SIZE;
    memcpy(buffer + *offset, payload, payload_len);
    *offset += payload_len;
}

static ssize_t memory_read(void *opaque, void *buffer, size_t length) {
    struct memory_reader *reader = opaque;
    size_t available;
    size_t count;

    if (reader->offset >= reader->len)
        return 0;

    available = reader->len - reader->offset;
    count = length < available ? length : available;

    if (reader->max_chunk && count > reader->max_chunk)
        count = reader->max_chunk;

    memcpy(buffer, reader->data + reader->offset, count);
    reader->offset += count;
    return (ssize_t)count;
}

static void test_fragmented_and_generations(void) {
    static const uint8_t p1[] = {0, 0, 0, 1, 0x67, 1, 2};
    static const uint8_t p2[] = {0, 0, 1, 0x65, 3, 4, 5};
    uint8_t buffer[256];
    size_t length = 0;
    struct memory_reader input;
    struct fh86_stream_reader reader;
    struct fh86_stream_frame frame;

    append_frame(buffer, &length, p1, sizeof(p1), 1000, 7, 1);
    append_frame(buffer, &length, p2, sizeof(p2), 2000, 7, 2);
    append_frame(buffer, &length, p1, sizeof(p1), 3000, 8, 3);
    append_frame(buffer, &length, p2, sizeof(p2), 4000, 7, 4);

    input = (struct memory_reader){buffer, length, 0, 3};
    fh86_stream_reader_init(&reader, memory_read, &input, 1024);

    assert(fh86_stream_next(&reader, &frame) == FH86_STREAM_OK);
    assert(frame.generation == 7);
    assert(frame.generation_changed == 1);
    assert(frame.pts_us == 1000);
    assert(frame.flags == 1);
    fh86_stream_frame_release(&frame);

    assert(fh86_stream_next(&reader, &frame) == FH86_STREAM_OK);
    assert(frame.generation == 7);
    assert(frame.generation_changed == 0);
    assert(frame.pts_us == 2000);
    fh86_stream_frame_release(&frame);

    assert(fh86_stream_next(&reader, &frame) == FH86_STREAM_OK);
    assert(frame.generation == 8);
    assert(frame.generation_changed == 1);
    fh86_stream_frame_release(&frame);

    assert(fh86_stream_next(&reader, &frame) == FH86_STREAM_STALE);
    assert(frame.data == NULL);

    assert(fh86_stream_next(&reader, &frame) == FH86_STREAM_ERR_EOF);
}

static void test_truncated_payload(void) {
    static const uint8_t payload[] = {0, 0, 0, 1, 0x65};
    uint8_t buffer[64];
    size_t length = 0;
    struct memory_reader input;
    struct fh86_stream_reader reader;
    struct fh86_stream_frame frame;

    append_frame(buffer, &length, payload, sizeof(payload), 1, 1, 0);
    length -= 2;

    input = (struct memory_reader){buffer, length, 0, 2};
    fh86_stream_reader_init(&reader, memory_read, &input, 1024);

    assert(fh86_stream_next(&reader, &frame) ==
        FH86_STREAM_ERR_TRUNCATED);
}

static void test_invalid_annexb(void) {
    static const uint8_t payload[] = {1, 2, 3, 4, 5};
    uint8_t buffer[64];
    size_t length = 0;
    struct memory_reader input;
    struct fh86_stream_reader reader;
    struct fh86_stream_frame frame;

    append_frame(buffer, &length, payload, sizeof(payload), 1, 1, 0);

    input = (struct memory_reader){buffer, length, 0, 0};
    fh86_stream_reader_init(&reader, memory_read, &input, 1024);

    assert(fh86_stream_next(&reader, &frame) ==
        FH86_STREAM_ERR_ANNEXB);
}

static void test_bad_header(void) {
    uint8_t buffer[FH86_WIRE_HEADER_SIZE] = {0};
    struct memory_reader input = {
        buffer, sizeof(buffer), 0, 5
    };
    struct fh86_stream_reader reader;
    struct fh86_stream_frame frame;

    memcpy(buffer, "BAD!", 4);

    fh86_stream_reader_init(&reader, memory_read, &input, 1024);

    assert(fh86_stream_next(&reader, &frame) ==
        FH86_STREAM_ERR_WIRE);
}

static void test_oversized_payload_rejected_before_payload_read(void) {
    uint8_t header[FH86_WIRE_HEADER_SIZE] = {0};
    struct memory_reader input = {
        header, sizeof(header), 0, 0
    };
    struct fh86_stream_reader reader;
    struct fh86_stream_frame frame;

    memcpy(header, "FH86", 4);
    put_be32(header + 4, FH86_WIRE_VERSION);
    put_be32(header + 8, 4096);
    put_be64(header + 24, 1);

    fh86_stream_reader_init(&reader, memory_read, &input, 1024);

    assert(fh86_stream_next(&reader, &frame) == FH86_STREAM_ERR_WIRE);
    assert(input.offset == FH86_WIRE_HEADER_SIZE);
    assert(frame.data == NULL);
}

static void test_truncated_header(void) {
    uint8_t header[FH86_WIRE_HEADER_SIZE] = {0};
    struct memory_reader input = {
        header, FH86_WIRE_HEADER_SIZE - 7, 0, 3
    };
    struct fh86_stream_reader reader;
    struct fh86_stream_frame frame;

    memcpy(header, "FH86", 4);
    put_be32(header + 4, FH86_WIRE_VERSION);
    put_be32(header + 8, 8);

    fh86_stream_reader_init(&reader, memory_read, &input, 1024);

    assert(fh86_stream_next(&reader, &frame) == FH86_STREAM_ERR_TRUNCATED);
    assert(frame.data == NULL);
}

static void test_session_reset_accepts_generation_restart(void) {
    static const uint8_t payload[] = {0, 0, 0, 1, 0x65, 1, 2};
    uint8_t first[64], second[64];
    size_t first_len = 0, second_len = 0;
    struct memory_reader input;
    struct fh86_stream_reader reader;
    struct fh86_stream_frame frame;

    append_frame(first, &first_len, payload, sizeof(payload), 9000, 9, 0);
    append_frame(second, &second_len, payload, sizeof(payload), 1000, 1, 0);

    input = (struct memory_reader){first, first_len, 0, 0};
    fh86_stream_reader_init(&reader, memory_read, &input, 1024);
    assert(fh86_stream_next(&reader, &frame) == FH86_STREAM_OK);
    assert(frame.generation == 9 && frame.generation_changed == 1);
    fh86_stream_frame_release(&frame);

    fh86_stream_reader_reset_session(&reader);
    input = (struct memory_reader){second, second_len, 0, 0};
    reader.opaque = &input;
    assert(fh86_stream_next(&reader, &frame) == FH86_STREAM_OK);
    assert(frame.generation == 1 && frame.generation_changed == 1);
    fh86_stream_frame_release(&frame);
}

int main(void) {
    test_fragmented_and_generations();
    test_truncated_payload();
    test_invalid_annexb();
    test_bad_header();
    test_oversized_payload_rejected_before_payload_read();
    test_truncated_header();
    test_session_reset_accepts_generation_restart();

    puts("fh86_stream PASS");
    return 0;
}
