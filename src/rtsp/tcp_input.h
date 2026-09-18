#pragma once

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>

/* Bounded RTSP headers/body; interleaved RTCP is discarded incrementally,
 * including packets larger than this buffer. No stdio on nonblocking input. */
struct rtsp_tcp_input { unsigned used, skip; char data[8192]; };

static inline void rtsp_tcp_consume(struct rtsp_tcp_input *s, unsigned n) {
    s->used -= n;
    memmove(s->data, s->data + n, s->used);
}

/* 1: complete RTSP request copied, 0: need more TCP bytes, -1: invalid. */
static inline int rtsp_tcp_extract(struct rtsp_tcp_input *s, char *out,
    unsigned *length) {
    for (;;) {
        if (s->skip) {
            unsigned n = s->used < s->skip ? s->used : s->skip;
            s->skip -= n;
            rtsp_tcp_consume(s, n);
            if (s->skip) return 0;
        }
        if (!s->used) return 0;
        if (s->data[0] == '$') {
            if (s->used < 4) return 0;
            s->skip = ((unsigned char)s->data[2] << 8) |
                (unsigned char)s->data[3];
            rtsp_tcp_consume(s, 4);
            continue;
        }
        unsigned h = 0, body = 0;
        for (unsigned i = 3; i < s->used; ++i)
            if (!memcmp(s->data + i - 3, "\r\n\r\n", 4)) {
                h = i + 1;
                break;
            }
        if (!h) return s->used == sizeof(s->data) ? -1 : 0;
        for (unsigned i = 0; i + 15 < h; ++i) {
            if ((i == 0 || s->data[i - 1] == '\n') &&
                !strncasecmp(s->data + i, "Content-Length:", 15)) {
                unsigned j = i + 15, digits = 0;
                while (j < h && (s->data[j] == ' ' || s->data[j] == '\t')) ++j;
                while (j < h && s->data[j] >= '0' && s->data[j] <= '9') {
                    body = body * 10 + (unsigned)(s->data[j++] - '0');
                    if (body > sizeof(s->data)) return -1;
                    ++digits;
                }
                if (!digits || j >= h || s->data[j] != '\r') return -1;
            }
        }
        if (h + body >= sizeof(s->data)) return -1;
        if (s->used < h + body) return 0;
        memcpy(out, s->data, h + body);
        *length = h + body;
        rtsp_tcp_consume(s, *length);
        return 1;
    }
}

/* -2 peer EOF/error, -1 invalid request, 0 would block, 1 complete request. */
static inline int rtsp_tcp_next(int fd, struct rtsp_tcp_input *s,
    char *out, unsigned *length) {
    for (;;) {
        int rc = rtsp_tcp_extract(s, out, length);
        if (rc) return rc;
        ssize_t n = recv(fd, s->data + s->used,
            sizeof(s->data) - s->used, MSG_DONTWAIT);
        if (n > 0) {
            s->used += (unsigned)n;
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
        return -2;
    }
}
