#include "fh8626_contract.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static void test_abi_sizes(void)
{
    assert(sizeof(struct fh8626_mem3) == 12u);
    assert(sizeof(struct fh8626_vpu_query) == 16u);
    assert(sizeof(struct fh8626_vpu_mem) == 24u);
    assert(sizeof(struct fh8626_channel_cfg) == 12u);
    assert(sizeof(struct fh8626_pae_mem_query) == 20u);
    assert(sizeof(struct fh8626_pae_mem) == 28u);
    assert(sizeof(struct fh8626_pae_cfg) == 44u);
}

static void test_lifecycle(void)
{
    struct fh8626_lifecycle life;
    fh8626_lifecycle_reset(&life);
    assert(life.state == FH8626_LIFE_COLD);
    assert(fh8626_lifecycle_apply(&life, FH8626_LIFE_VIDEO_CREATE) == -EPERM);
    assert(fh8626_lifecycle_apply(&life, FH8626_LIFE_HAL_INIT) == 0);
    assert(fh8626_lifecycle_apply(&life, FH8626_LIFE_SYSTEM_INIT) == 0);
    assert(fh8626_lifecycle_apply(&life, FH8626_LIFE_PIPELINE_CREATE) == 0);
    assert(fh8626_lifecycle_apply(&life, FH8626_LIFE_VIDEO_CREATE) == 0);
    assert(fh8626_lifecycle_apply(&life, FH8626_LIFE_STREAM_START) == 0);
    assert(fh8626_lifecycle_lease_begin(&life) == 0);
    assert(fh8626_lifecycle_lease_begin(&life) == -EBUSY);
    assert(fh8626_lifecycle_apply(&life, FH8626_LIFE_STREAM_STOP) == -EBUSY);
    assert(fh8626_lifecycle_lease_release(&life) == 0);
    assert(fh8626_lifecycle_balanced(&life));
    assert(fh8626_lifecycle_apply(&life, FH8626_LIFE_STREAM_STOP) == 0);
    assert(fh8626_lifecycle_apply(&life, FH8626_LIFE_VIDEO_DESTROY) == 0);
    assert(fh8626_lifecycle_apply(&life, FH8626_LIFE_PIPELINE_DESTROY) == 0);
    assert(fh8626_lifecycle_apply(&life, FH8626_LIFE_SYSTEM_DEINIT) == 0);
    assert(fh8626_lifecycle_apply(&life, FH8626_LIFE_HAL_DEINIT) == 0);
    assert(life.state == FH8626_LIFE_COLD);
}

static void test_stream_descriptor(void)
{
    uint32_t desc[FH8626_MEDIA_STREAM_DESC_WORDS];
    struct fh8626_stream_span span;
    memset(desc, 0, sizeof(desc));
    desc[1] = FH8626_MEDIA_STREAM_KIND;
    desc[7] = 0x10001000u;
    desc[8] = 0x800u;
    desc[10] = 0x12345678u;
    assert(fh8626_stream_decode(desc, 0x10000000u, 0x2000u, &span) == 0);
    assert(span.first == 0x10001000u);
    assert(span.first_len == 0x800u);
    assert(span.second_len == 0u);
    assert(span.timestamp_raw == 0x12345678u);

    desc[7] = 0x10001c00u;
    desc[8] = 0x800u;
    assert(fh8626_stream_decode(desc, 0x10000000u, 0x2000u, &span) == 0);
    assert(span.first == 0x10001c00u);
    assert(span.first_len == 0x400u);
    assert(span.second == 0x10000000u);
    assert(span.second_len == 0x400u);

    desc[8] = 0x3000u;
    assert(fh8626_stream_decode(desc, 0x10000000u, 0x2000u, &span) == -ERANGE);
}

static void test_video_contract(void)
{
    hal_vidconfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.width = FH8626_NATIVE_WIDTH;
    cfg.height = FH8626_NATIVE_HEIGHT;
    cfg.codec = HAL_VIDCODEC_H264;
    cfg.profile = HAL_VIDPROFILE_BASELINE;
    cfg.framerate = FH8626_NATIVE_FPS;
    cfg.gop = 25;
    cfg.mode = HAL_VIDMODE_CBR;
    cfg.bitrate = 2048;
    assert(fh8626_video_contract_known(&cfg));
    cfg.profile = HAL_VIDPROFILE_MAIN;
    assert(!fh8626_video_contract_known(&cfg));
    cfg.profile = HAL_VIDPROFILE_BASELINE;
    cfg.gop = 50;
    assert(!fh8626_video_contract_known(&cfg));
    cfg.gop = 25;
    cfg.mode = HAL_VIDMODE_ABR;
    assert(!fh8626_video_contract_known(&cfg));
    cfg.mode = HAL_VIDMODE_CBR;
    cfg.width = 1920;
    assert(!fh8626_video_contract_known(&cfg));
    cfg.width = FH8626_NATIVE_WIDTH;
    cfg.codec = HAL_VIDCODEC_H265;
    assert(!fh8626_video_contract_known(&cfg));
}

static void test_capabilities(void)
{
    struct fh8626_capabilities caps = fh8626_capabilities_current();
    assert(caps.h264_720p25 == FH8626_CAP_PROVEN);
    assert(caps.stream_lease_release == FH8626_CAP_PROVEN);
    assert(caps.sensor_open_backend == FH8626_CAP_UNRESOLVED);
    assert(caps.same_boot_full_teardown == FH8626_CAP_UNRESOLVED);
    assert(caps.idr_request == FH8626_CAP_PROVEN);
    assert(caps.rate_control_mapping == FH8626_CAP_PROVEN);
    assert(caps.vpss_1080p_scaling == FH8626_CAP_UNRESOLVED);
    assert(caps.h265 == FH8626_CAP_UNSUPPORTED);
    assert(caps.jpeg_snapshot == FH8626_CAP_UNRESOLVED);
    assert(caps.mjpeg == FH8626_CAP_UNRESOLVED);
    assert(caps.audio_rtx_transport == FH8626_CAP_PROVEN);
    assert(caps.audio == FH8626_CAP_UNRESOLVED);
    assert(caps.runtime_video_reconfigure == FH8626_CAP_UNRESOLVED);
    assert(caps.temperature == FH8626_CAP_UNSUPPORTED);
    assert(!strcmp(fh8626_capability_state_name(FH8626_CAP_PROVEN), "proven"));
    assert(!strcmp(fh8626_capability_state_name(FH8626_CAP_UNRESOLVED), "unresolved"));
    assert(!strcmp(fh8626_capability_state_name(FH8626_CAP_UNSUPPORTED), "unsupported"));
}

int main(void)
{
    test_abi_sizes();
    test_lifecycle();
    test_stream_descriptor();
    test_video_contract();
    test_capabilities();
    puts("fh8626_contract PASS");
    return 0;
}
