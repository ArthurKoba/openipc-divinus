#pragma once

#include <stddef.h>
#include <stdint.h>

struct fh86_live_diag {
    int running;
    int have_frame;
    uint64_t frames_received;
    uint64_t frames_forwarded;
    uint64_t tiny_frames;
    uint64_t pack_errors;
    uint64_t generation;
    uint64_t generation_changes;
    uint64_t last_frame_bytes;
    uint64_t last_pts_us;
    uint64_t last_frame_mono_ms;
};

void fh86_live_diag_reset(struct fh86_live_diag *diag, int running);
void fh86_live_diag_set_running(struct fh86_live_diag *diag, int running);
void fh86_live_diag_observe(struct fh86_live_diag *diag,
    size_t frame_bytes, uint64_t pts_us, uint64_t generation,
    int generation_changed, int forwarded, int pack_error,
    uint64_t monotonic_ms);
int fh86_live_diag_write_json(const struct fh86_live_diag *diag,
    uint64_t now_ms, char *buffer, size_t size);
