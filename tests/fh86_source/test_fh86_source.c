#include "../../src/source/fh86_source.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

struct callback_state {
    int frames;
    uint64_t generation[4];
    uint64_t pts[4];
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

static int send_bad_header(int fd) {
    uint8_t header[FH86_WIRE_HEADER_SIZE] = {0};

    memcpy(header, "BAD!", 4);
    put_be32(header + 4, FH86_WIRE_VERSION);
    put_be32(header + 8, 8);

    return send_all(fd, header, sizeof(header));
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
        listen(fd, 4) != 0) {
        close(fd);
        unlink(path);
        return -1;
    }

    return fd;
}

static void collect_frame(void *opaque,
    const struct fh86_stream_frame *frame) {
    struct callback_state *state = opaque;
    int index = state->frames;

    assert(index < 4);
    state->generation[index] = frame->generation;
    state->pts[index] = frame->pts_us;
    state->frames++;
}

static int stop_after_one(void *opaque) {
    const struct callback_state *state = opaque;
    return state->frames >= 1;
}

static int stop_after_two(void *opaque) {
    const struct callback_state *state = opaque;
    return state->frames >= 2;
}

static void test_reconnect_and_generation_restart(void) {
    char path[sizeof(((struct sockaddr_un *)0)->sun_path)];
    struct fh86_source_config config;
    struct fh86_source source;
    struct callback_state state = {0};
    int listener;
    pid_t child;
    int status;

    snprintf(path, sizeof(path), "/tmp/fh86-source-%ld.sock",
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

    config.path = path;
    config.max_payload = 1024;
    config.connect_timeout_ms = 1000;
    config.read_timeout_ms = 1000;
    config.reconnect_delay_ms = 10;

    assert(fh86_source_init(&source, &config,
        collect_frame, &state) == FH86_SOURCE_OK);
    assert(fh86_source_run(&source,
        stop_after_two, &state) == FH86_SOURCE_OK);

    assert(state.frames == 2);
    assert(state.generation[0] == 9);
    assert(state.generation[1] == 1);
    assert(state.pts[0] == 9000);
    assert(state.pts[1] == 1000);
    assert(source.stats.connections == 2);
    assert(source.stats.reconnects == 1);
    assert(source.stats.frames == 2);
    assert(source.stats.generation_changes == 2);

    fh86_source_close(&source);
    close(listener);

    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status) == 0);

    unlink(path);
}

static void test_protocol_error_reconnect(void) {
    char path[sizeof(((struct sockaddr_un *)0)->sun_path)];
    struct fh86_source_config config;
    struct fh86_source source;
    struct callback_state state = {0};
    int listener;
    pid_t child;
    int status;

    snprintf(path, sizeof(path), "/tmp/fh86-proto-%ld.sock",
        (long)getpid());

    listener = create_listener(path);
    assert(listener >= 0);

    child = fork();
    assert(child >= 0);

    if (child == 0) {
        int client;

        client = accept(listener, NULL, NULL);
        if (client < 0 || send_bad_header(client) != 0)
            _exit(2);
        close(client);

        client = accept(listener, NULL, NULL);
        if (client < 0 || send_frame(client, 1, 1234) != 0)
            _exit(3);
        close(client);
        close(listener);
        _exit(0);
    }

    config.path = path;
    config.max_payload = 1024;
    config.connect_timeout_ms = 1000;
    config.read_timeout_ms = 1000;
    config.reconnect_delay_ms = 10;

    assert(fh86_source_init(&source, &config,
        collect_frame, &state) == FH86_SOURCE_OK);

    assert(fh86_source_run(&source,
        stop_after_one, &state) == FH86_SOURCE_OK);

    assert(state.frames == 1);
    assert(source.stats.protocol_errors >= 1);
    assert(source.stats.connections >= 2);

    fh86_source_close(&source);
    close(listener);

    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status) == 0);

    unlink(path);
}

int main(void) {
    test_reconnect_and_generation_restart();
    test_protocol_error_reconnect();

    puts("fh86_source PASS");
    return 0;
}
