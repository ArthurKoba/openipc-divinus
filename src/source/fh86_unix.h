#pragma once

#include <stddef.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

struct fh86_unix_transport {
    int fd;
    int connect_timeout_ms;
    int read_timeout_ms;
    struct sockaddr_un addr;
    socklen_t addr_len;
};

enum fh86_unix_status {
    FH86_UNIX_OK = 0,
    FH86_UNIX_ERR_ARGUMENT = -1,
    FH86_UNIX_ERR_PATH = -2,
    FH86_UNIX_ERR_SOCKET = -3,
    FH86_UNIX_ERR_CONNECT = -4,
    FH86_UNIX_ERR_TIMEOUT = -5
};

int fh86_unix_transport_init(struct fh86_unix_transport *transport,
    const char *path, int connect_timeout_ms, int read_timeout_ms);

int fh86_unix_transport_connect(struct fh86_unix_transport *transport);

void fh86_unix_transport_close(struct fh86_unix_transport *transport);

ssize_t fh86_unix_transport_read(void *opaque, void *buffer, size_t length);
