#define _POSIX_C_SOURCE 200809L

#include "fh86_unix.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int64_t monotonic_ms(void) {
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return -1;

    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int wait_fd(int fd, short events, int timeout_ms) {
    struct pollfd pollfd;
    int64_t deadline;

    deadline = monotonic_ms();
    if (deadline < 0)
        return -1;

    deadline += timeout_ms;

    pollfd.fd = fd;
    pollfd.events = events;
    pollfd.revents = 0;

    for (;;) {
        int64_t now;
        int64_t remaining;
        int wait_ms;
        int status;

        now = monotonic_ms();
        if (now < 0)
            return -1;

        remaining = deadline - now;
        if (remaining < 0)
            remaining = 0;

        wait_ms = remaining > INT_MAX ? INT_MAX : (int)remaining;

        status = poll(&pollfd, 1, wait_ms);
        if (status > 0)
            return 0;

        if (status == 0) {
            errno = ETIMEDOUT;
            return -1;
        }

        if (errno != EINTR)
            return -1;
    }
}

static int set_fd_flags(int fd) {
    int flags;

    flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
        return -1;

    flags = fcntl(fd, F_GETFD, 0);
    if (flags < 0 || fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0)
        return -1;

    return 0;
}

int fh86_unix_transport_init(struct fh86_unix_transport *transport,
    const char *path, int connect_timeout_ms, int read_timeout_ms) {
    size_t path_len;

    if (!transport || !path || !path[0] ||
        connect_timeout_ms < 0 || read_timeout_ms < 0)
        return FH86_UNIX_ERR_ARGUMENT;

    path_len = strlen(path);
    if (path_len >= sizeof(transport->addr.sun_path))
        return FH86_UNIX_ERR_PATH;

    memset(transport, 0, sizeof(*transport));
    transport->fd = -1;
    transport->connect_timeout_ms = connect_timeout_ms;
    transport->read_timeout_ms = read_timeout_ms;
    transport->addr.sun_family = AF_UNIX;
    memcpy(transport->addr.sun_path, path, path_len + 1);
    transport->addr_len = (socklen_t)(
        offsetof(struct sockaddr_un, sun_path) + path_len + 1);

    return FH86_UNIX_OK;
}

void fh86_unix_transport_close(struct fh86_unix_transport *transport) {
    if (!transport)
        return;

    if (transport->fd >= 0)
        close(transport->fd);

    transport->fd = -1;
}

int fh86_unix_transport_connect(struct fh86_unix_transport *transport) {
    int fd;
    int error;
    socklen_t error_len = sizeof(error);

    if (!transport || !transport->addr.sun_path[0])
        return FH86_UNIX_ERR_ARGUMENT;

    fh86_unix_transport_close(transport);

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return FH86_UNIX_ERR_SOCKET;

    if (set_fd_flags(fd) != 0) {
        close(fd);
        return FH86_UNIX_ERR_SOCKET;
    }

    if (connect(fd, (const struct sockaddr *)&transport->addr,
            transport->addr_len) != 0) {
        if (errno != EINPROGRESS) {
            close(fd);
            return FH86_UNIX_ERR_CONNECT;
        }

        if (wait_fd(fd, POLLOUT, transport->connect_timeout_ms) != 0) {
            int saved_errno = errno;

            close(fd);
            errno = saved_errno;

            if (errno == ETIMEDOUT)
                return FH86_UNIX_ERR_TIMEOUT;

            return FH86_UNIX_ERR_CONNECT;
        }

        if (getsockopt(fd, SOL_SOCKET, SO_ERROR,
                &error, &error_len) != 0) {
            close(fd);
            return FH86_UNIX_ERR_CONNECT;
        }

        if (error != 0) {
            close(fd);
            errno = error;

            if (error == ETIMEDOUT)
                return FH86_UNIX_ERR_TIMEOUT;

            return FH86_UNIX_ERR_CONNECT;
        }
    }

    transport->fd = fd;
    return FH86_UNIX_OK;
}

ssize_t fh86_unix_transport_read(void *opaque, void *buffer, size_t length) {
    struct fh86_unix_transport *transport = opaque;

    if (!transport || transport->fd < 0 || (!buffer && length)) {
        errno = EINVAL;
        return -1;
    }

    if (!length)
        return 0;

    for (;;) {
        ssize_t count;

        if (wait_fd(transport->fd, POLLIN,
                transport->read_timeout_ms) != 0)
            return -1;

        count = recv(transport->fd, buffer, length, 0);
        if (count >= 0)
            return count;

        if (errno != EINTR && errno != EAGAIN &&
            errno != EWOULDBLOCK)
            return -1;
    }
}
