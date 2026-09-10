#include "../../src/source/fh86_stream.h"
#include "../../src/source/fh86_unix.h"

#include <assert.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

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

static int send_all(int fd, const uint8_t *data, size_t length) {
    size_t offset = 0;

    while (offset < length) {
        ssize_t count = send(fd, data + offset, length - offset, 0);

        if (count <= 0)
            return -1;

        offset += (size_t)count;
    }

    return 0;
}

static int send_frame(int fd, uint64_t generation, uint64_t pts) {
    static const uint8_t payload[] = {
        0, 0, 0, 1, 0x65, 0x11, 0x22, 0x33
    };
    uint8_t frame[FH86_WIRE_HEADER_SIZE + sizeof(payload)];

    memset(frame, 0, sizeof(frame));
    memcpy(frame, "FH86", 4);
    put_be32(frame + 4, FH86_WIRE_VERSION);
    put_be32(frame + 8, sizeof(payload));
    put_be32(frame + 12, 1);
    put_be64(frame + 16, pts);
    put_be64(frame + 24, generation);
    memcpy(frame + FH86_WIRE_HEADER_SIZE, payload, sizeof(payload));

    return send_all(fd, frame, sizeof(frame));
}

static int create_listener(const char *path) {
    struct sockaddr_un addr;
    int fd;

    unlink(path);

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, path);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        listen(fd, 2) != 0) {
        close(fd);
        unlink(path);
        return -1;
    }

    return fd;
}

static void test_restart_resets_generation(void) {
    char path[sizeof(((struct sockaddr_un *)0)->sun_path)];
    struct fh86_unix_transport transport;
    struct fh86_stream_reader reader;
    struct fh86_stream_frame frame;
    int listener;
    pid_t child;
    int status;

    snprintf(path, sizeof(path), "/tmp/fh86-unix-%ld.sock",
        (long)getpid());

    listener = create_listener(path);
    assert(listener >= 0);

    child = fork();
    assert(child >= 0);

    if (child == 0) {
        int client;

        client = accept(listener, NULL, NULL);
        if (client < 0 || send_frame(client, 9, 9000) != 0)
            _exit(2);
        close(client);

        client = accept(listener, NULL, NULL);
        if (client < 0 || send_frame(client, 1, 1000) != 0)
            _exit(3);
        close(client);
        close(listener);
        _exit(0);
    }

    assert(fh86_unix_transport_init(&transport,
        path, 1000, 1000) == FH86_UNIX_OK);

    assert(fh86_unix_transport_connect(&transport) == FH86_UNIX_OK);

    fh86_stream_reader_init(&reader, fh86_unix_transport_read,
        &transport, 1024);

    assert(fh86_stream_next(&reader, &frame) == FH86_STREAM_OK);
    assert(frame.generation == 9);
    assert(frame.generation_changed == 1);
    fh86_stream_frame_release(&frame);

    fh86_unix_transport_close(&transport);
    fh86_stream_reader_reset_session(&reader);

    assert(fh86_unix_transport_connect(&transport) == FH86_UNIX_OK);
    assert(fh86_stream_next(&reader, &frame) == FH86_STREAM_OK);
    assert(frame.generation == 1);
    assert(frame.generation_changed == 1);
    fh86_stream_frame_release(&frame);

    fh86_unix_transport_close(&transport);
    close(listener);

    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status) == 0);

    unlink(path);
}

static void test_read_timeout(void) {
    char path[sizeof(((struct sockaddr_un *)0)->sun_path)];
    struct fh86_unix_transport transport;
    struct fh86_stream_reader reader;
    struct fh86_stream_frame frame;
    int listener;
    pid_t child;
    int status;

    snprintf(path, sizeof(path), "/tmp/fh86-timeout-%ld.sock",
        (long)getpid());

    listener = create_listener(path);
    assert(listener >= 0);

    child = fork();
    assert(child >= 0);

    if (child == 0) {
        int client = accept(listener, NULL, NULL);

        if (client < 0)
            _exit(2);

        poll(NULL, 0, 150);
        close(client);
        close(listener);
        _exit(0);
    }

    assert(fh86_unix_transport_init(&transport,
        path, 1000, 30) == FH86_UNIX_OK);
    assert(fh86_unix_transport_connect(&transport) == FH86_UNIX_OK);

    fh86_stream_reader_init(&reader, fh86_unix_transport_read,
        &transport, 1024);

    assert(fh86_stream_next(&reader, &frame) ==
        FH86_STREAM_ERR_TIMEOUT);

    fh86_unix_transport_close(&transport);
    close(listener);

    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status) == 0);

    unlink(path);
}

int main(void) {
    test_restart_resets_generation();
    test_read_timeout();

    puts("fh86_unix PASS");
    return 0;
}
