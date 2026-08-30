#include "fh86_live_diag.h"

#include <stdio.h>
#include <string.h>

#define FH86_TINY_FRAME_BYTES 128u
#define FH86_STREAMING_AGE_MS 2000u

void fh86_live_diag_reset(struct fh86_live_diag *diag, int running) {
    if (!diag)
        return;
    memset(diag, 0, sizeof(*diag));
    diag->running = running != 0;
}

void fh86_live_diag_set_running(struct fh86_live_diag *diag, int running) {
    if (!diag)
        return;
    diag->running = running != 0;
}

void fh86_live_diag_observe(struct fh86_live_diag *diag,
    size_t frame_bytes, uint64_t pts_us, uint64_t generation,
    int generation_changed, int forwarded, int pack_error,
    uint64_t monotonic_ms) {
    if (!diag)
        return;

    diag->frames_received++;
    if (forwarded)
        diag->frames_forwarded++;
    if (frame_bytes < FH86_TINY_FRAME_BYTES)
        diag->tiny_frames++;
    if (pack_error)
        diag->pack_errors++;
    if (generation_changed)
        diag->generation_changes++;

    diag->generation = generation;
    diag->last_frame_bytes = frame_bytes;
    diag->last_pts_us = pts_us;
    diag->last_frame_mono_ms = monotonic_ms;
    diag->have_frame = 1;
}

int fh86_live_diag_write_json(const struct fh86_live_diag *diag,
    uint64_t now_ms, char *buffer, size_t size) {
    const char *state;
    uint64_t age_ms = 0;
    int written;

    if (!diag || !buffer || !size)
        return -1;

    if (!diag->running)
        state = "stopped";
    else if (!diag->have_frame)
        state = "waiting";
    else {
        age_ms = now_ms >= diag->last_frame_mono_ms ?
            now_ms - diag->last_frame_mono_ms : 0;
        state = age_ms <= FH86_STREAMING_AGE_MS ? "streaming" : "stalled";
    }

    if (diag->have_frame) {
        written = snprintf(buffer, size,
            "{\"source\":\"fh86\",\"state\":\"%s\",\"running\":%s,"
            "\"frames_received\":%llu,\"frames_forwarded\":%llu,"
            "\"tiny_frames\":%llu,\"pack_errors\":%llu,"
            "\"generation\":%llu,\"generation_changes\":%llu,"
            "\"last_frame_bytes\":%llu,\"last_pts_us\":%llu,"
            "\"last_frame_age_ms\":%llu}",
            state, diag->running ? "true" : "false",
            (unsigned long long)diag->frames_received,
            (unsigned long long)diag->frames_forwarded,
            (unsigned long long)diag->tiny_frames,
            (unsigned long long)diag->pack_errors,
            (unsigned long long)diag->generation,
            (unsigned long long)diag->generation_changes,
            (unsigned long long)diag->last_frame_bytes,
            (unsigned long long)diag->last_pts_us,
            (unsigned long long)age_ms);
    } else {
        written = snprintf(buffer, size,
            "{\"source\":\"fh86\",\"state\":\"%s\",\"running\":%s,"
            "\"frames_received\":%llu,\"frames_forwarded\":%llu,"
            "\"tiny_frames\":%llu,\"pack_errors\":%llu,"
            "\"generation\":%llu,\"generation_changes\":%llu,"
            "\"last_frame_bytes\":0,\"last_pts_us\":0,"
            "\"last_frame_age_ms\":null}",
            state, diag->running ? "true" : "false",
            (unsigned long long)diag->frames_received,
            (unsigned long long)diag->frames_forwarded,
            (unsigned long long)diag->tiny_frames,
            (unsigned long long)diag->pack_errors,
            (unsigned long long)diag->generation,
            (unsigned long long)diag->generation_changes);
    }

    if (written < 0 || (size_t)written >= size)
        return -1;
    return written;
}
