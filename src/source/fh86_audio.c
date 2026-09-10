#include "fh86_audio.h"

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define FH86_PCM_FRAME_BYTES 320

static pthread_t capture_thread;
static volatile int capture_running;
static int capture_fd = -1;
static pid_t capture_pid = -1;
static fh86_audio_frame_cb capture_callback;

static uint64_t monotonic_ms(void) {
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now)) return 0;
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
}

static void *capture_main(void *opaque) {
    unsigned char pcm[FH86_PCM_FRAME_BYTES];
    unsigned int sequence = 0;
    size_t used = 0;

    (void)opaque;
    while (capture_running) {
        ssize_t got = read(capture_fd, pcm + used, sizeof(pcm) - used);
        if (got > 0) {
            hal_audframe frame;

            used += (size_t)got;
            if (used != sizeof(pcm)) continue;
            memset(&frame, 0, sizeof(frame));
            frame.channelCnt = 1;
            frame.data[0] = pcm;
            frame.length[0] = sizeof(pcm);
            frame.seq = sequence++;
            frame.timestamp = monotonic_ms();
            if (capture_callback) capture_callback(&frame);
            used = 0;
            continue;
        }
        if (got < 0 && errno == EINTR) continue;
        break;
    }
    capture_running = 0;
    return NULL;
}

int fh86_audio_start(fh86_audio_frame_cb callback) {
    int pipefd[2];
    int status;

    if (capture_running) return EXIT_SUCCESS;
    if (!callback) return EXIT_FAILURE;
    if (pipe(pipefd)) return EXIT_FAILURE;

    capture_pid = fork();
    if (capture_pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return EXIT_FAILURE;
    }
    if (capture_pid == 0) {
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        execl("/usr/sbin/fh8626-audio", "fh8626-audio",
              "stream", (char *)NULL);
        _exit(127);
    }

    close(pipefd[1]);
    capture_fd = pipefd[0];
    capture_callback = callback;
    capture_running = 1;
    status = pthread_create(&capture_thread, NULL, capture_main, NULL);
    if (status) {
        capture_running = 0;
        close(capture_fd);
        capture_fd = -1;
        kill(capture_pid, SIGTERM);
        waitpid(capture_pid, NULL, 0);
        capture_pid = -1;
        capture_callback = NULL;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

void fh86_audio_stop(void) {
    pid_t pid = capture_pid;

    if (pid > 0) kill(pid, SIGTERM);
    capture_running = 0;
    if (capture_fd >= 0) close(capture_fd);
    capture_fd = -1;
    if (capture_thread) pthread_join(capture_thread, NULL);
    capture_thread = 0;
    if (pid > 0) waitpid(pid, NULL, 0);
    capture_pid = -1;
    capture_callback = NULL;
}

int fh86_audio_running(void) {
    return capture_running;
}
