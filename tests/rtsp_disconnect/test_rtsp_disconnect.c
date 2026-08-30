#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

static int connect_once(const char *host, unsigned short port, unsigned seq)
{
    struct sockaddr_in addr;
    char request[256];
    char response[512];
    int fd;
    int len;
    ssize_t got;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        close(fd);
        return -1;
    }

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }

    len = snprintf(request, sizeof(request),
        "OPTIONS rtsp://%s:%u/ RTSP/1.0\r\nCSeq: %u\r\n\r\n",
        host, port, seq);
    if (len <= 0 || (size_t)len >= sizeof(request)) {
        close(fd);
        return -1;
    }

    if (send(fd, request, (size_t)len, 0) != len) {
        close(fd);
        return -1;
    }

    got = recv(fd, response, sizeof(response) - 1, 0);
    if (got <= 0) {
        close(fd);
        return -1;
    }
    response[got] = '\0';
    if (!strstr(response, "RTSP/1.0 200 OK")) {
        close(fd);
        return -1;
    }

    /* Deliberately close without TEARDOWN, matching VLC/client churn. */
    close(fd);
    return 0;
}

int main(int argc, char **argv)
{
    const char *host = "127.0.0.1";
    unsigned long port = 28554;
    unsigned i;

    if (argc > 1)
        host = argv[1];
    if (argc > 2) {
        char *end = NULL;
        errno = 0;
        port = strtoul(argv[2], &end, 10);
        if (errno || !end || *end || port == 0 || port > 65535)
            return 2;
    }

    for (i = 1; i <= 32; ++i) {
        if (connect_once(host, (unsigned short)port, i) != 0) {
            fprintf(stderr, "disconnect iteration %u failed\n", i);
            return 1;
        }
        usleep(20000);
    }

    puts("rtsp_disconnect_client PASS");
    return 0;
}
