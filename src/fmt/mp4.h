#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bitbuf.h"
#include "moof.h"
#include "moov.h"
#include "nal.h"
#include "../hal/macros.h"
#include "../hal/types.h"

extern uint32_t default_sample_size;

struct Mp4State {
    bool header_sent;

    uint32_t sequence_number;
    uint64_t base_data_offset;
    uint64_t video_media_decode_time;
    uint64_t audio_media_decode_time;
    uint32_t default_sample_duration;

    uint32_t nals_count;
};

void mp4_set_config(short width, short height, char framerate, char acodec,
    unsigned short bitrate, char channels, unsigned int srate);

/* Called under mp4Mtx. FH86 timestamps are microseconds, other HALs opt out.
 * Prepare once per access unit, then share the fragment with HTTP/recording.
 * Returns 1 for a fragment, 0 for headers only, -1 for invalid input. */
int mp4_prepare_pack(const hal_vidpack *pack, char is_h265, bool capture_timing);
void mp4_capture_discontinuity(void);
bool mp4_fragment_is_key(void);

void mp4_set_sps(const char *nal_data, const uint32_t nal_len, char is_h265);
void mp4_set_pps(const char *nal_data, const uint32_t nal_len, char is_h265);
void mp4_set_vps(const char *nal_data, const uint32_t nal_len);
enum BufError mp4_set_slice(const char *nal_data, const uint32_t nal_len,
    char is_iframe);
enum BufError mp4_ingest_audio(const char *data, const uint32_t len);

enum BufError mp4_set_state(struct Mp4State *state);

enum BufError mp4_get_header(struct BitBuf *ptr);
enum BufError mp4_get_moof(struct BitBuf *ptr);
enum BufError mp4_get_mdat(struct BitBuf *ptr);
