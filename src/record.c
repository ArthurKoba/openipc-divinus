#include "record.h"

static FILE *recordFile;
static struct Mp4State recordState;
static size_t recordSize;
time_t recordStartTime = 0;
char recordOn = 0, recordPath[256];

static void record_check_segment_size(void) {
    if (app_config.record_segment_size > 0 &&
        recordSize >= (size_t)app_config.record_segment_size) {
        record_stop();
        record_start();
    }
}

static bool record_write(const void *data, size_t length) {
    if (!recordOn || !recordFile) return false;
    if (fwrite(data, 1, length, recordFile) != length) {
        HAL_DANGER("record", "Failed to write recording data; stopping recording.\n");
        record_stop();
        return false;
    }
    recordSize += length;
    return true;
}

static void record_check_segment_duration() {
    if (app_config.record_segment_duration <= 0) return;

    time_t currentTime = time(NULL);
    if (currentTime == (time_t)-1 || recordStartTime == (time_t)-1) return;

    if (currentTime - recordStartTime >= app_config.record_segment_duration) {
        record_stop();
        record_start();
    }
}

void record_start(void) {
    char filename[160];
    int length;

    if (recordOn) return;

    if (recordFile) {
        HAL_DANGER("record", "Output file needs to be closed before initializing a new one.\n");
        return;
    }

    recordSize = 0;
    recordState.header_sent = false;
    recordStartTime = time(NULL);

    if (EMPTY(app_config.record_path)) {
        HAL_DANGER("record", "Destination path is not set!\n");
        return;
    }

    if (!EMPTY(app_config.record_filename)) {
        length = snprintf(filename, sizeof(filename), "%s",
            app_config.record_filename);
    } else {
        struct tm tm_buf, *tm_info = localtime_r(&recordStartTime, &tm_buf);
        length = tm_info ? (int)strftime(filename, sizeof(filename),
            "recording_%Y%m%d_%H%M%S.mp4", tm_info) : 0;
    }
    if (length <= 0 || (size_t)length >= sizeof(filename)) {
        HAL_DANGER("record", "Recording filename is invalid or too long!\n");
        return;
    }
    length = snprintf(recordPath, sizeof(recordPath), "%s%s%s",
        app_config.record_path,
        app_config.record_path[strlen(app_config.record_path) - 1] == '/' ? "" : "/",
        filename);
    if (length <= 0 || (size_t)length >= sizeof(recordPath)) {
        HAL_DANGER("record", "Recording destination path is too long!\n");
        return;
    }

    if (!(recordFile = fopen(recordPath, "wb"))) {
        HAL_DANGER("record", "Failed to open the destination file!\n");
        return;
    }

    recordOn = 1;
}

void record_stop(void) {
    if (!recordOn) return;

    if (!recordFile) {
        HAL_DANGER("record", "No output file is opened and ready to finalize!\n");
        return;
    }

    fclose(recordFile);
    recordFile = NULL;

    recordOn = 0;
    recordStartTime = 0;
}

void send_mp4_to_record(hal_vidstream *stream, char isH265) {
    if (!recordOn) return;

    if (!recordFile) {
        HAL_DANGER("record", "No output file is opened for writing data!\n");
        return;
    }

    for (unsigned int i = 0; i < stream->count; ++i) {
        /* Fragment prepared once by save_video_stream under mp4Mtx. */

        static enum BufError err;
        static char len_buf[50];
        if (!recordState.header_sent) {
            if (!mp4_fragment_is_key()) continue;
            struct BitBuf header_buf;
            err = mp4_get_header(&header_buf); chk_err_continue
            if (!record_write(header_buf.buf, header_buf.offset)) return;

            recordState.sequence_number = 0;
            recordState.base_data_offset = header_buf.offset;
            recordState.video_media_decode_time = 0;
            recordState.audio_media_decode_time = 0;
            recordState.header_sent = true;
            recordState.nals_count = 0;
            recordState.default_sample_duration =
                default_sample_size;
        }

        err = mp4_set_state(&recordState); chk_err_continue
        {
            struct BitBuf moof_buf;
            err = mp4_get_moof(&moof_buf); chk_err_continue
            if (!record_write(moof_buf.buf, moof_buf.offset)) return;
        }
        {
            struct BitBuf mdat_buf;
            err = mp4_get_mdat(&mdat_buf); chk_err_continue
            if (!record_write(mdat_buf.buf, mdat_buf.offset)) return;
        }
        /* Rotate only after a complete fragment. Rotating between moof and
         * mdat would create two invalid MP4 files. */
        record_check_segment_size();
    }

    record_check_segment_duration();
}
