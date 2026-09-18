#include "mp4.h"

uint32_t default_sample_size = 40000;
uint32_t last_video_duration, last_audio_duration, timescale;

unsigned int aud_samplerate = 0;
unsigned short aud_bitrate = 0;
char aud_channels = 0, aud_codec = 0, vid_framerate = 30;
short vid_width = 1920, vid_height = 1080;

char buf_pps[128];
uint16_t buf_pps_len = 0;
char buf_sps[128];
uint16_t buf_sps_len = 0;
char buf_vps[128];
uint16_t buf_vps_len = 0;
struct BitBuf buf_aud;
struct BitBuf buf_header;
struct BitBuf buf_mdat;
struct BitBuf buf_moof;
static bool capture_clock, capture_seen;
static uint64_t capture_previous;
static uint32_t capture_duration;
static uint32_t fragment_duration;
static struct BitBuf access_unit;
static bool fragment_key;

bool mp4_fragment_is_key(void) { return fragment_key; }

void mp4_capture_discontinuity(void) {
    capture_seen = false;
}

enum BufError create_header(char is_h265) {
    if (buf_header.offset > 0)
        return BUF_OK;
    if (buf_sps_len == 0)
        return BUF_OK;
    if (buf_pps_len == 0)
        return BUF_OK;
    if (is_h265 && buf_vps_len == 0)
        return BUF_OK;

    struct MoovInfo moov_info;
    memset(&moov_info, 0, sizeof(struct MoovInfo));
    moov_info.audio_codec = aud_codec;
    moov_info.audio_bitrate = aud_bitrate;
    moov_info.audio_channels = aud_channels;
    moov_info.audio_samplerate = aud_samplerate;
    moov_info.is_h265 = is_h265 & 1;
    moov_info.profile_idc = 100;
    moov_info.level_idc = 41;
    moov_info.width = vid_width;
    moov_info.height = vid_height;
    moov_info.horizontal_resolution = 0x00480000; // 72 dpi
    moov_info.vertical_resolution = 0x00480000;   // 72 dpi
    moov_info.creation_time = 0;
    timescale = capture_clock ? 1000000u : default_sample_size * vid_framerate;
    moov_info.timescale = timescale;
    moov_info.sps = buf_sps;
    moov_info.sps_length = buf_sps_len;
    moov_info.pps = buf_pps;
    moov_info.pps_length = buf_pps_len;
    moov_info.vps = buf_vps;
    moov_info.vps_length = buf_vps_len;

    buf_aud.offset = 0;
    buf_header.offset = 0;
    enum BufError err = write_header(&buf_header, &moov_info);
    chk_err return BUF_OK;
}

void mp4_set_config(short width, short height, char framerate, char acodec,
    unsigned short bitrate, char channels, unsigned int srate) {
    vid_width = width;
    vid_height = height;
    vid_framerate = framerate;
    aud_codec = acodec;
    aud_bitrate = bitrate;
    aud_channels = channels;
    aud_samplerate = srate;
}

void mp4_set_sps(const char *nal_data, const uint32_t nal_len, char is_h265) {
    buf_sps_len = MIN(nal_len, (uint32_t)sizeof(buf_sps));
    memcpy(buf_sps, nal_data, buf_sps_len);
    create_header(is_h265);
}

void mp4_set_pps(const char *nal_data, const uint32_t nal_len, char is_h265) {
    buf_pps_len = MIN(nal_len, (uint32_t)sizeof(buf_pps));
    memcpy(buf_pps, nal_data, buf_pps_len);
    create_header(is_h265);
}

void mp4_set_vps(const char *nal_data, const uint32_t nal_len) {
    buf_vps_len = MIN(nal_len, (uint32_t)sizeof(buf_vps));
    memcpy(buf_vps, nal_data, buf_vps_len);
    create_header(1);
}

static enum BufError mp4_set_sample(const char *data, uint32_t size,
    char is_iframe, bool length_prefixed) {
    uint64_t aud_ticks = 0;
    enum BufError err;

    struct SampleInfo samples_info[2];
    memset(samples_info, 0, sizeof(samples_info));
    samples_info[0].size = size + (length_prefixed ? 0 : 4);
    samples_info[0].duration = fragment_duration ? fragment_duration : default_sample_size;
    last_video_duration = samples_info[0].duration;
    samples_info[0].flags = is_iframe ? 0 : 65536;
    fragment_key = is_iframe != 0;
    samples_info[1].size = buf_aud.offset;
    if (aud_bitrate > 0 && buf_aud.offset > 0) {
        aud_ticks = ((uint64_t)buf_aud.offset * 8 * timescale) /
            (aud_bitrate * 1000);
        samples_info[1].duration = (uint32_t)aud_ticks;
        last_audio_duration = samples_info[1].duration;
    } else {
        samples_info[1].duration = 0;
        last_audio_duration = 0;
    }

    buf_moof.offset = 0;
    err = write_moof(
        &buf_moof, 0, 0, 0, 0, default_sample_size, samples_info,
        1, samples_info + 1, 1);
    chk_err;

    buf_mdat.offset = 0;
    if (length_prefixed) {
        err = put_u32_be(&buf_mdat, 8 + size + buf_aud.offset); chk_err;
        err = put_str4(&buf_mdat, "mdat"); chk_err;
        err = put(&buf_mdat, data, size); chk_err;
        err = put(&buf_mdat, buf_aud.buf, buf_aud.offset);
    } else {
        err = write_mdat(&buf_mdat, data, size, buf_aud.buf, buf_aud.offset);
    }
    chk_err;

    buf_aud.offset = 0;

    return BUF_OK;
}

enum BufError mp4_set_slice(const char *data, uint32_t size, char is_iframe) {
    return mp4_set_sample(data, size, is_iframe, false);
}

int mp4_prepare_pack(const hal_vidpack *pack, char is_h265, bool automatic) {
    bool key = false;
    if (!pack || !pack->data || pack->offset > pack->length ||
        pack->naluCnt < 0 || pack->naluCnt > 8 || pack->length > 4194304u)
        return -1;
    /* Mode must be selected before SPS/PPS create the movie header. */
    if (capture_clock != automatic) {
        if (buf_header.offset) return -1; /* never change an active timebase */
        capture_clock = automatic;
        capture_seen = false;
    }
    unsigned int available = pack->length - pack->offset;
    const unsigned char *data = pack->data + pack->offset;
    access_unit.offset = 0;
    unsigned int previous_end = 0;
    /* Validate the whole access unit before modifying parameter caches. */
    for (int i = 0; i < pack->naluCnt; ++i) {
        const hal_vidnalu *nal = &pack->nalu[i];
        if (nal->offset < previous_end || nal->offset > available || nal->length > available - nal->offset ||
            nal->length < 4) return -1;
        previous_end = nal->offset + nal->length;
        const unsigned char *p = data + nal->offset;
        unsigned int sc = p[0] == 0 && p[1] == 0 && p[2] == 1 ? 3 :
            (p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 1 ? 4 : 0);
        if (!sc || nal->length <= sc) return -1;
        unsigned int type = is_h265 ? ((p[sc] >> 1) & 63) : (p[sc] & 31);
        if ((!is_h265 && (type == 7 || type == 8)) ||
            (is_h265 && (type == 32 || type == 33 || type == 34))) {
            if (nal->length - sc > 128) return -1;
        }
    }
    for (int i = 0; i < pack->naluCnt; ++i) {
        const hal_vidnalu *nal = &pack->nalu[i];
        const unsigned char *p = data + nal->offset;
        unsigned int sc = p[2] == 1 ? 3 : 4;
        unsigned int size = nal->length - sc;
        const char *payload = (const char *)p + sc;
        unsigned int type = is_h265 ? ((p[sc] >> 1) & 63) : (p[sc] & 31);
        if (type == (is_h265 ? 33u : 7u)) mp4_set_sps(payload, size, is_h265);
        else if (type == (is_h265 ? 34u : 8u)) mp4_set_pps(payload, size, is_h265);
        else if (is_h265 && type == 32) mp4_set_vps(payload, size);
        else if (is_h265 ? type < 32 : (type == 1 || type == 5)) {
            if (put_u32_be(&access_unit, size) != BUF_OK ||
                put(&access_unit, payload, size) != BUF_OK) return -1;
            key |= is_h265 ? (type >= 16 && type <= 21) : type == 5;
        }
    }
    /* A header-only descriptor must never resend the preceding moof/mdat. */
    if (!access_unit.offset || !buf_header.offset) return 0;
    fragment_duration = default_sample_size;
    if (automatic) {
        uint32_t duration = capture_duration ? capture_duration :
            1000000u / (vid_framerate > 0 ? (unsigned char)vid_framerate : 25u);
        if (capture_seen && pack->timestamp > capture_previous &&
            pack->timestamp - capture_previous <= 2000000u)
            duration = (uint32_t)(pack->timestamp - capture_previous);
        /* Previous interval is used without retaining a DMA/sidecar buffer.
         * Initial/rebased sample uses fallback; error is bounded to one frame,
         * rather than accumulating the configured-vs-source FPS difference. */
        capture_previous = pack->timestamp;
        capture_seen = true;
        capture_duration = duration;
        fragment_duration = duration;
    }
    return mp4_set_sample(access_unit.buf, access_unit.offset, key, true) == BUF_OK ? 1 : -1;
}

enum BufError mp4_ingest_audio(const char *data, const uint32_t len) {
    enum BufError err;
    /* No unbounded accumulation if video stops or MP4 is disabled. */
    if (len > 131072u || buf_aud.offset > 131072u - len)
        return BUF_INCORRECT;
    err = put(&buf_aud, data, len);
    chk_err;

    return BUF_OK;
}

enum BufError mp4_set_state(struct Mp4State *state) {
    enum BufError err = BUF_OK;
    if (pos_sequence_number > 0)
        err = put_u32_be_to_offset(
            &buf_moof, pos_sequence_number, state->sequence_number);
    chk_err if (pos_base_data_offset > 0) err = put_u64_be_to_offset(
        &buf_moof, pos_base_data_offset, state->base_data_offset);
    chk_err if (pos_audio_media_decode_time > 0) err = put_u64_be_to_offset(
        &buf_moof, pos_audio_media_decode_time,
        state->audio_media_decode_time);
    chk_err if (pos_video_media_decode_time > 0) err = put_u64_be_to_offset(
        &buf_moof, pos_video_media_decode_time,
        state->video_media_decode_time);
    chk_err state->sequence_number++;
    state->base_data_offset += buf_moof.offset + buf_mdat.offset;
    state->video_media_decode_time += last_video_duration;
    state->audio_media_decode_time += last_audio_duration;
    return BUF_OK;
}

enum BufError mp4_get_header(struct BitBuf *ptr) {
    ptr->buf = buf_header.buf;
    ptr->size = buf_header.size;
    ptr->offset = buf_header.offset;
    return BUF_OK;
}

enum BufError mp4_get_mdat(struct BitBuf *ptr) {
    ptr->buf = buf_mdat.buf;
    ptr->size = buf_mdat.size;
    ptr->offset = buf_mdat.offset;
    return BUF_OK;
}

enum BufError mp4_get_moof(struct BitBuf *ptr) {
    ptr->buf = buf_moof.buf;
    ptr->size = buf_moof.size;
    ptr->offset = buf_moof.offset;
    return BUF_OK;
}
