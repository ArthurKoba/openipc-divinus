#include "fh8626_audio.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

/*
 * Native FH8626V100 RTX capture backend.
 *
 * This is the hardware-proven Apollo ACW transaction reduced to the capture
 * path Divinus actually owns. Speaker amplifier mute and other retail-board
 * GPIO policy deliberately do not live here.
 */
#define RTXBUS_RESET 0x40000000UL
#define RTXBUS_COMMAND 0x20000000UL

#define AC_CMD_AI_ENABLE 6U
#define AC_CMD_AI_DISABLE 7U
#define AC_CMD_AI_VOLUME 15U

#define FH8626_CAPTURE_RATE 8000U
#define FH8626_CAPTURE_PACKET 320U
#define FH8626_CAPTURE_VOLUME 31U
#define FH8626_CAPTURE_NR 1U
#define FH8626_CAPTURE_NR_LEVEL 3U

struct ac_init_command {
    uint32_t size;
    uint16_t size_a;
    uint16_t size_b;
    uint32_t opcode;
    int32_t status;
    uint32_t reserved;
    uint32_t map_offset;
    uint32_t map_length;
    uint32_t tail_length;
};

struct ac_simple_command {
    uint32_t size;
    uint16_t size_a;
    uint16_t size_b;
    uint32_t opcode;
    int32_t status;
    uint32_t value;
};

struct ac_config_command {
    uint32_t size;
    uint16_t size_a;
    uint16_t size_b;
    uint32_t opcode;
    int32_t status;
    uint32_t config[7];
    uint32_t selector;
};

struct ac_frame_command {
    uint32_t size;
    uint16_t size_a;
    uint16_t size_b;
    uint32_t opcode;
    int32_t status;
    uint32_t data_length;
    uint32_t data_offset;
    uint32_t pts_low;
    uint32_t pts_high;
};

struct ac_init_params_command {
    uint32_t size;
    uint16_t size_a;
    uint16_t size_b;
    uint32_t opcode;
    int32_t status;
    uint32_t reserved;
    uint32_t blob_length;
    uint8_t blob[0x17a];
};

struct ac_pair_command {
    uint32_t size;
    uint16_t size_a;
    uint16_t size_b;
    uint32_t opcode;
    int32_t status;
    uint32_t values[2];
};

/* Exact AJL33PQ0866/FH8626 AC init payload captured from the validated stock
 * path. It configures the Fullhan audio DSP, not external board GPIO policy. */
static const uint8_t retail_init_params[0x17a] = {
    0x03,0x00,0xe6,0x00,0x01,0x00,0x00,0x00,0x01,0x00,0xd8,0xff,
    0xec,0xff,0xf4,0xff,0xf4,0xff,0xf4,0xff,0xf4,0xff,0xfd,0xff,
    0x11,0x00,0x12,0x00,0x0a,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0xc4,0xff,0xfc,0xff,0xfe,0xff,0xf4,0xff,0xf6,0xff,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0xb0,0xff,0xb0,0xff,0x0f,0x00,0xc8,0x00,0xc8,0x00,
    0x2c,0x01,0xb0,0x04,0xb0,0x04,0xb0,0x04,0xb0,0x04,0xb0,0x04,
    0xb0,0x04,0xb0,0x04,0xb0,0x04,0x58,0x02,0x58,0x02,0x58,0x02,
    0x58,0x02,0x58,0x02,0x58,0x02,0x58,0x02,0x58,0x02,0x58,0x02,
    0x58,0x02,0x58,0x02,0xe8,0x03,0xe8,0x03,0xe8,0x03,0xe8,0x03,
    0xe8,0x03,0xe8,0x03,0xe8,0x03,0xe8,0x03,0xe8,0x03,0xbc,0x02,
    0xbc,0x02,0xbc,0x02,0xb0,0x04,0xb0,0x04,0xb0,0x04,0xb0,0x04,
    0xb0,0x04,0xb0,0x04,0xb0,0x04,0xb0,0x04,0xb0,0x04,0xb0,0x04,
    0xbc,0x02,0xbc,0x02,0xbc,0x02,0xbc,0x02,0xbc,0x02,0xbc,0x02,
    0xbc,0x02,0xbc,0x02,0xbc,0x02,0xbc,0x02,0xbc,0x02,0x20,0x03,
    0x20,0x03,0x20,0x03,0x20,0x03,0x20,0x03,0x20,0x03,0x20,0x03,
    0x20,0x03,0x20,0x03,0x32,0x00,0x32,0x00,0xc8,0x00,0xf4,0x01,
    0xb0,0x04,0xb0,0x04,0xb0,0x04,0xb0,0x04,0xb0,0x04,0xb0,0x04,
    0xb0,0x04,0xb0,0x04,0xb0,0x04,0xb0,0x04,0xb0,0x04,0xb0,0x04,
    0xb0,0x04,0xb0,0x04,0xb0,0x04,0xb0,0x04,0xb0,0x04,0xb0,0x04,
    0xb0,0x04,0xb0,0x04,0xb0,0x04,0xb0,0x04,0xb0,0x04,0xb0,0x04,
    0xb0,0x04,0xb0,0x04,0xb0,0x04,0xb0,0x04,0xb0,0x04,0xb0,0x04,
    0xb0,0x04,0xb0,0x04,0xb0,0x04,0xb0,0x04,0xb0,0x04,0xb0,0x04,
    0xb0,0x04,0xb0,0x04,0xb0,0x04,0xb0,0x04,0xb0,0x04,0xb0,0x04,
    0xb0,0x04,0xb0,0x04,0xb0,0x04,0xb0,0x04,0xb0,0x04,0xb0,0x04,
    0xb0,0x04,0xb0,0x04,0xb0,0x04,0xb0,0x04,0xb0,0x04,0xb0,0x04,
    0xb0,0x04,0xb0,0x04,0xb0,0x04,0xb0,0x04,0xb0,0x04,0xb0,0x04,
    0xb0,0x04,0x03,0x00,0x01,0x00,0x36,0x0e,0x76,0x14,0xb4,0x10,
    0x5e,0x18,0x00,0x00,0x01,0x00,0x84,0x18,0x04,0x00,0x08,0x00,
    0x02,0x00,0x0c,0x00,0x02,0x00,0x00,0x00,0x23,0x00,0x19,0x00,
    0x0f,0x00,0x14,0x00,0x01,0x00,0x28,0x4b,0x88,0x3b,0x00,0x00,
    0x0c,0x00,0x64,0x00,0x40,0x1f
};

static pthread_t capture_thread;
static int capture_thread_started;
static volatile int capture_running;
static int capture_fd = -1;
static uint8_t *capture_shared = MAP_FAILED;
static uint32_t capture_map_offset;
static uint32_t capture_map_length;
static int capture_ai_enabled;
static fh8626_audio_frame_cb capture_callback;
static int capture_last_error;

static uint64_t monotonic_ms(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now))
        return 0;
    return (uint64_t)now.tv_sec * 1000u +
        (uint64_t)now.tv_nsec / 1000000u;
}

static int ac_command_status(int fd, void *request)
{
    if (ioctl(fd, RTXBUS_COMMAND, request) < 0)
        return -errno;
    return 0;
}

static int ac_simple(int fd, uint32_t command, uint32_t value)
{
    struct ac_simple_command request = {
        .size = 12,
        .size_a = 12,
        .size_b = 12,
        .opcode = 0x01000000U | command |
            (command == 3U ? 0U : 0x00040000U),
        .value = value,
    };
    int rc = ac_command_status(fd, &request);

    if (rc)
        return rc;
    return request.status ? -EIO : 0;
}

static int ac_set_init_params(int fd)
{
    struct ac_init_params_command request = {
        .size = 0x18a,
        .size_a = 0x18a,
        .size_b = 0x18a,
        .opcode = 0x01040022,
        .blob_length = sizeof(retail_init_params),
    };
    int rc;

    memcpy(request.blob, retail_init_params, sizeof(retail_init_params));
    rc = ac_command_status(fd, &request);
    if (rc)
        return rc;
    return request.status ? -EIO : 0;
}

static int ac_set_capture_nr(int fd, uint32_t enabled, uint32_t level)
{
    struct ac_pair_command request = {
        .size = 16,
        .size_a = 16,
        .size_b = 16,
        .opcode = 0x01040013,
        .values = { enabled, level },
    };
    int rc = ac_command_status(fd, &request);

    if (rc)
        return rc;
    return request.status ? -EIO : 0;
}

static int ac_set_capture_config(int fd)
{
    struct ac_config_command request = {
        .size = 8,
        .size_a = 8,
        .size_b = 0x28,
        .opcode = 0x01040005,
        .config = {
            0, FH8626_CAPTURE_RATE, 16, 0, 1,
            FH8626_CAPTURE_PACKET, FH8626_CAPTURE_VOLUME
        },
        .selector = 4,
    };
    int rc = ac_command_status(fd, &request);

    if (rc)
        return rc;
    return request.status ? -EIO : 0;
}

static void capture_release(void)
{
    if (capture_ai_enabled && capture_fd >= 0)
        (void)ac_simple(capture_fd, AC_CMD_AI_DISABLE, 0);
    capture_ai_enabled = 0;

    if (capture_shared != MAP_FAILED) {
        munmap(capture_shared, capture_map_length);
        capture_shared = MAP_FAILED;
    }
    capture_map_offset = 0;
    capture_map_length = 0;

    if (capture_fd >= 0)
        close(capture_fd);
    capture_fd = -1;
    capture_callback = NULL;
}

static void *capture_main(void *opaque)
{
    unsigned int sequence = 0;
    uint64_t last_frame_ms = monotonic_ms();
    (void)opaque;

    while (capture_running) {
        struct ac_frame_command request = {
            .size = 0x18,
            .size_a = 0x18,
            .size_b = 8,
            .opcode = 0x01008000,
        };
        uint32_t relative;
        hal_audframe frame;
        int rc = ac_command_status(capture_fd, &request);

        if (rc) {
            if (rc == -EINTR)
                continue;
            capture_last_error = rc;
            break;
        }
        if (request.status || !request.data_length) {
            uint64_t now_ms = monotonic_ms();
            if (last_frame_ms && now_ms &&
                now_ms - last_frame_ms > 10000u) {
                capture_last_error = -ETIMEDOUT;
                break;
            }
            usleep(10000);
            continue;
        }
        last_frame_ms = monotonic_ms();
        if (request.data_offset < capture_map_offset) {
            capture_last_error = -ERANGE;
            break;
        }

        relative = request.data_offset - capture_map_offset;
        if (relative > capture_map_length ||
            request.data_length > capture_map_length - relative) {
            capture_last_error = -ERANGE;
            break;
        }

        /* The Divinus path deliberately accepts only the hardware-validated
         * packet contract. Do not silently generalize untested RTX framing. */
        if (request.data_length != FH8626_CAPTURE_PACKET) {
            capture_last_error = -EMSGSIZE;
            break;
        }

        memset(&frame, 0, sizeof(frame));
        frame.channelCnt = 1;
        frame.data[0] = capture_shared + relative;
        frame.length[0] = request.data_length;
        frame.seq = sequence++;
        frame.timestamp = (unsigned int)monotonic_ms();
        if (capture_callback)
            (void)capture_callback(&frame);
    }

    capture_running = 0;
    if (capture_last_error)
        fprintf(stderr, "FH8626 RTX capture stopped with error %#x\n",
            capture_last_error);
    return NULL;
}

int fh8626_audio_start(fh8626_audio_frame_cb callback)
{
    struct ac_init_command init = {
        .size = 0x18,
        .size_a = 0x18,
        .size_b = 0x18,
        .opcode = 0x01040004,
    };
    int rc;

    if (capture_running)
        return 0;
    if (!callback)
        return -EINVAL;

    capture_last_error = 0;
    capture_fd = open("/dev/rtxbus", O_RDWR | O_CLOEXEC);
    if (capture_fd < 0)
        return -errno;

    if (ioctl(capture_fd, RTXBUS_RESET, 0) != 0) {
        rc = errno ? -errno : -EIO;
        goto fail;
    }
    rc = ac_command_status(capture_fd, &init);
    if (rc)
        goto fail;
    if (init.status || !init.map_length) {
        rc = -EIO;
        goto fail;
    }

    capture_map_offset = init.map_offset;
    capture_map_length = init.map_length;
    capture_shared = mmap(NULL, capture_map_length, PROT_READ | PROT_WRITE,
        MAP_SHARED, capture_fd, (off_t)capture_map_offset);
    if (capture_shared == MAP_FAILED) {
        rc = -errno;
        goto fail;
    }

    rc = ac_set_init_params(capture_fd);
    if (rc)
        goto fail;
    rc = ac_set_capture_nr(capture_fd, FH8626_CAPTURE_NR,
        FH8626_CAPTURE_NR_LEVEL);
    if (rc)
        goto fail;
    rc = ac_set_capture_config(capture_fd);
    if (rc)
        goto fail;
    rc = ac_simple(capture_fd, AC_CMD_AI_ENABLE, 0);
    if (rc)
        goto fail;
    capture_ai_enabled = 1;
    rc = ac_simple(capture_fd, AC_CMD_AI_VOLUME, FH8626_CAPTURE_VOLUME);
    if (rc)
        goto fail;

    capture_callback = callback;
    capture_running = 1;
    rc = pthread_create(&capture_thread, NULL, capture_main, NULL);
    if (rc) {
        capture_running = 0;
        rc = -rc;
        goto fail;
    }
    capture_thread_started = 1;
    return 0;

fail:
    capture_running = 0;
    capture_release();
    return rc;
}

void fh8626_audio_stop(void)
{
    capture_running = 0;
    if (capture_thread_started) {
        pthread_join(capture_thread, NULL);
        capture_thread_started = 0;
    }
    capture_release();
}

int fh8626_audio_running(void)
{
    return capture_running;
}

int fh8626_audio_last_error(void)
{
    return capture_last_error;
}
