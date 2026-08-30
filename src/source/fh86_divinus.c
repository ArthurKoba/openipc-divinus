#include "fh86_divinus.h"

#include "fh86_h264.h"
#include "fh86_live_diag.h"
#include "fh86_source.h"
#include "../app_config.h"
#include "../hal/globals.h"
#include "../hal/macros.h"
#include "../media.h"
#include "../fmt/mp4.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static struct fh86_source source;
static hal_chnstate source_channel[1];
static pthread_t source_thread;
static char source_running;
static unsigned int source_sequence;
static uint64_t source_pack_errors;
static pthread_mutex_t source_diag_lock = PTHREAD_MUTEX_INITIALIZER;
static struct fh86_live_diag source_diag;

static uint64_t monotonic_ms(void) {
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static int should_stop(void *opaque) {
    (void)opaque;
    return !source_running || !keepRunning;
}

static void handle_frame(void *opaque, const struct fh86_stream_frame *frame) {
    struct fh86_h264_pack_result result;
    hal_vidstream stream;
    int pack_status;

    (void)opaque;

    pack_status = fh86_h264_build_pack(frame, &result);
    pthread_mutex_lock(&source_diag_lock);
    fh86_live_diag_observe(&source_diag, frame->len, frame->pts_us,
        frame->generation, frame->generation_changed,
        pack_status == FH86_H264_OK, pack_status != FH86_H264_OK,
        monotonic_ms());
    pthread_mutex_unlock(&source_diag_lock);

    if (pack_status != FH86_H264_OK) {
        source_pack_errors++;
        return;
    }

    stream.pack = &result.pack;
    stream.count = 1;
    stream.seq = source_sequence++;
    save_video_stream(0, &stream);
}

static void *source_thread_main(void *opaque) {
    (void)opaque;
    fh86_source_run(&source, should_stop, NULL);
    return NULL;
}

int fh86_divinus_start(void) {
    struct fh86_source_config config;
    pthread_attr_t attr;
    size_t default_stack;
    int status;

    if (source_running)
        return EXIT_SUCCESS;
    if (app_config.source_type != APP_SOURCE_FH86)
        return EXIT_FAILURE;

    memset(source_channel, 0, sizeof(source_channel));
    source_channel[0].enable = 1;
    source_channel[0].mainLoop = 1;
    source_channel[0].payload = HAL_VIDCODEC_H264;
    chnState = source_channel;
    chnCount = 1;
    source_sequence = 0;
    source_pack_errors = 0;
    pthread_mutex_lock(&source_diag_lock);
    fh86_live_diag_reset(&source_diag, 1);
    pthread_mutex_unlock(&source_diag_lock);

    config.path = app_config.source_path;
    config.max_payload = app_config.source_max_payload;
    config.connect_timeout_ms = (int)app_config.source_connect_timeout_ms;
    config.read_timeout_ms = (int)app_config.source_read_timeout_ms;
    config.reconnect_delay_ms = (int)app_config.source_reconnect_delay_ms;

    status = fh86_source_init(&source, &config, handle_frame, NULL);
    if (status != FH86_SOURCE_OK) {
        chnState = NULL;
        chnCount = 0;
        return EXIT_FAILURE;
    }

    if (app_config.mp4_enable)
        mp4_set_config(app_config.mp4_width, app_config.mp4_height,
            app_config.mp4_fps, HAL_AUDCODEC_UNSPEC, 0, 1, 0);

    source_running = 1;

    pthread_attr_init(&attr);
    pthread_attr_getstacksize(&attr, &default_stack);
    if (pthread_attr_setstacksize(&attr,
            app_config.venc_stream_thread_stack_size))
        HAL_DANGER("fh86_source", "Can't set source thread stack size!\n");

    status = pthread_create(&source_thread, &attr,
        source_thread_main, NULL);
    if (pthread_attr_setstacksize(&attr, default_stack))
        HAL_DANGER("fh86_source", "Can't restore thread stack size!\n");
    pthread_attr_destroy(&attr);

    if (status) {
        source_running = 0;
        pthread_mutex_lock(&source_diag_lock);
        fh86_live_diag_set_running(&source_diag, 0);
        pthread_mutex_unlock(&source_diag_lock);
        fh86_source_close(&source);
        chnState = NULL;
        chnCount = 0;
        return EXIT_FAILURE;
    }

    HAL_INFO("fh86_source", "External H.264 source started.\n");
    return EXIT_SUCCESS;
}

void fh86_divinus_stop(void) {
    if (!source_running)
        return;

    source_running = 0;
    pthread_join(source_thread, NULL);
    fh86_source_close(&source);
    pthread_mutex_lock(&source_diag_lock);
    fh86_live_diag_set_running(&source_diag, 0);
    pthread_mutex_unlock(&source_diag_lock);

    HAL_INFO("fh86_source",
        "External source stopped: frames=%llu reconnects=%llu "
        "generation_changes=%llu stale=%llu timeouts=%llu protocol=%llu "
        "io=%llu pack_errors=%llu.\n",
        (unsigned long long)source.stats.frames,
        (unsigned long long)source.stats.reconnects,
        (unsigned long long)source.stats.generation_changes,
        (unsigned long long)source.stats.stale_frames,
        (unsigned long long)source.stats.timeouts,
        (unsigned long long)source.stats.protocol_errors,
        (unsigned long long)source.stats.io_errors,
        (unsigned long long)source_pack_errors);

    memset(source_channel, 0, sizeof(source_channel));
    chnState = NULL;
    chnCount = 0;
}

int fh86_divinus_write_status_json(char *buffer, size_t size) {
    struct fh86_live_diag snapshot;

    if (!buffer || !size)
        return -1;

    pthread_mutex_lock(&source_diag_lock);
    snapshot = source_diag;
    pthread_mutex_unlock(&source_diag_lock);

    return fh86_live_diag_write_json(&snapshot, monotonic_ms(), buffer, size);
}
