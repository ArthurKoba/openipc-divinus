#include "http_post.h"

#include <errno.h>

pthread_t httpPostPid = 0;

static int write_all(int fd, const void *buffer, size_t length) {
    const unsigned char *data = buffer;
    size_t offset = 0;

    while (offset < length) {
        ssize_t written = write(fd, data + offset, length - offset);
        if (written < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (written == 0)
            return -1;
        offset += (size_t)written;
    }

    return 0;
}

int http_post_send(hal_jpegdata *jpeg) {
    char *host_addr = app_config.http_post_host;
    int result = EXIT_FAILURE;

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1)
        HAL_ERROR("http_post", "Socket creation failed!\n");

    struct addrinfo *server_addr = NULL;
    int ret = getaddrinfo(host_addr, "80", NULL, &server_addr);
    if (!ret) {
        const struct addrinfo *r;
        for (r = server_addr; r != NULL; r = r->ai_next) {
            if (connect(sockfd, r->ai_addr, r->ai_addrlen) == 0)
                break;
        }
        if (!r)
            goto cleanup;

        HAL_INFO("http_post", "Successfully connected to %s!\n", host_addr);

        char time_url[256];
        {
            time_t timer;
            time(&timer);
            struct tm tm_buf, *tm_info = localtime_r(&timer, &tm_buf);
            if (!tm_info || !strftime(time_url, sizeof(time_url),
                    app_config.http_post_url, tm_info))
                goto cleanup;
        }

        char header_buf[1024];
        int buf_len = snprintf(
            header_buf, sizeof(header_buf),
            "PUT %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "User-Agent: Camera openipc.org\r\n"
            "Accept: */*\r\n"
            "Content-Type: image/jpeg\r\n"
            "Content-Length: %u\r\n",
            time_url, host_addr, jpeg->jpegSize);
        if (buf_len < 0 || buf_len >= (int)sizeof(header_buf) ||
            write_all(sockfd, header_buf, (size_t)buf_len) != 0)
            goto cleanup;

        if (app_config.http_post_login[0] && app_config.http_post_password[0]) {
            char log_pass[sizeof(app_config.http_post_login) +
                sizeof(app_config.http_post_password) + 2];
            int log_pass_len = snprintf(log_pass, sizeof(log_pass), "%s:%s",
                app_config.http_post_login, app_config.http_post_password);
            if (log_pass_len < 0 || log_pass_len >= (int)sizeof(log_pass))
                goto cleanup;

            char base64buf[1024];
            int base64_len = base64_encode(base64buf, log_pass, log_pass_len);
            if (base64_len <= 0 || base64_len >= (int)sizeof(base64buf))
                goto cleanup;

            static const char auth_prefix[] = "Authorization: Basic ";
            if (write_all(sockfd, auth_prefix, sizeof(auth_prefix) - 1) != 0 ||
                write_all(sockfd, base64buf, (size_t)base64_len) != 0 ||
                write_all(sockfd, "\r\n", 2) != 0)
                goto cleanup;
        }

        if (write_all(sockfd, "\r\n", 2) != 0 ||
            write_all(sockfd, jpeg->data, jpeg->jpegSize) != 0)
            goto cleanup;

        char reply[1024];
        if (read(sockfd, reply, sizeof(reply)) < 0)
            goto cleanup;

        result = EXIT_SUCCESS;
    }

cleanup:
    if (server_addr)
        freeaddrinfo(server_addr);
    close(sockfd);
    return result;
}

void *http_post_thread(void *arg) {
    (void)arg;
    hal_jpegdata jpeg = {0};
    jpeg.data = NULL;
    jpeg.length = 0;
    jpeg.jpegSize = 0;
    sleep(3);

    while (keepRunning) {
        static time_t last_time = 0;
        time_t current_time = time(NULL);
        if (current_time - last_time < app_config.http_post_interval) {
            sleep(1);
            continue;
        }

        if (jpeg_get(app_config.http_post_width, app_config.http_post_height,
                app_config.http_post_qfactor, 3, &jpeg)) {
            HAL_WARNING("http_post", "Grabbing the JPEG image has failed!\n");
            continue;
        }
        last_time = current_time;

        if (http_post_send(&jpeg)) {
            HAL_WARNING("http_post", "Sending the picture has failed!\n");
            continue;
        }
    }

    if (jpeg.data) {
        free(jpeg.data);
        jpeg.data = NULL;
    }

    return NULL;
}

void http_post_start(void) {
    pthread_attr_t thread_attr;
    pthread_attr_init(&thread_attr);
    size_t stacksize;
    pthread_attr_getstacksize(&thread_attr, &stacksize);
    size_t new_stacksize = 16 * 1024;
    if (pthread_attr_setstacksize(&thread_attr, new_stacksize))
        HAL_DANGER("http_post", "Can't set stack size %zu\n", new_stacksize);
    if (pthread_create(
            &httpPostPid, &thread_attr, http_post_thread, NULL))
        HAL_DANGER("http_post", "Starting the sender thread failed!\n");
    if (pthread_attr_setstacksize(&thread_attr, stacksize))
        HAL_DANGER("http_post", "Can't set stack size %zu\n", stacksize);
    pthread_attr_destroy(&thread_attr);
}

void http_post_stop(void) {
    pthread_join(httpPostPid, NULL);
}
