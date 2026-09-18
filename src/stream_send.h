#pragma once

#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <time.h>

static inline int64_t stream_now_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts)) return -1;
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static inline int stream_send_deadline(int fd, struct iovec *iov, int count,
    unsigned int budget_ms) {
    int64_t now = stream_now_ms();
    if (fd < 0 || now < 0) return -1;
    const int64_t deadline = now + budget_ms;
    while (count > 0) {
        now = stream_now_ms();
        if (now < 0 || now >= deadline) { errno = ETIMEDOUT; return -1; }
        struct msghdr msg = {0};
        msg.msg_iov = iov;
        msg.msg_iovlen = count;
        ssize_t n = sendmsg(fd, &msg, MSG_DONTWAIT | MSG_NOSIGNAL);
        if (n > 0) {
            while (count && (size_t)n >= iov->iov_len) {
                n -= iov->iov_len;
                ++iov; --count;
            }
            if (count && n) {
                iov->iov_base = (char *)iov->iov_base + n;
                iov->iov_len -= n;
            }
            continue;
        }
        if (!n) return -1;
        if (errno == EINTR) continue;
        if (errno != EAGAIN && errno != EWOULDBLOCK) return -1;
        now = stream_now_ms();
        if (now < 0 || now >= deadline) { errno = ETIMEDOUT; return -1; }
        struct pollfd pfd = {.fd = fd, .events = POLLOUT};
        int rc = poll(&pfd, 1, (int)(deadline - now));
        if (!rc) { errno = ETIMEDOUT; return -1; }
        if (rc < 0 && errno != EINTR) return -1;
        if (rc > 0 && (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))) return -1;
    }
    return 0;
}
