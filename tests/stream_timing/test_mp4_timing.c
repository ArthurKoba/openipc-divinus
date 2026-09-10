#include <assert.h>
#include <stdio.h>
#include "../../src/fmt/mp4.c"

static unsigned char bytes[] = {
    0,0,0,1,0x67,0x42,0,0x29,0x11,
    0,0,1,0x68,0x12,0x34,
    0,0,0,1,0x65,0x22,0x33,
    0,0,1,0x65,0x44,0x55
};
static hal_vidpack pack = {.data=bytes, .length=sizeof(bytes), .naluCnt=4,
    .nalu={{.offset=0,.length=9},{.offset=9,.length=6},
           {.offset=15,.length=7},{.offset=22,.length=6}}};

int main(void) {
    mp4_set_config(1920,1080,25,HAL_AUDCODEC_MP3,32,1,8000);
    pack.timestamp=1000000;
    assert(mp4_prepare_pack(&pack,0,true)==1);
    assert(timescale==1000000 && last_video_duration==40000);
    assert(mp4_fragment_is_key());
    /* Both VCL NALs retained in a single sample (two length prefixes). */
    assert(access_unit.offset==14 && buf_mdat.offset==22);
    struct Mp4State a={0}, b={0};
    assert(mp4_set_state(&a)==BUF_OK && mp4_set_state(&b)==BUF_OK);
    assert(a.video_media_decode_time==40000 && b.video_media_decode_time==40000);
    unsigned char audio[288]={0};
    assert(mp4_ingest_audio((char*)audio,sizeof(audio))==BUF_OK);
    hal_vidpack headers=pack;
    headers.length=15; headers.naluCnt=2;
    assert(mp4_prepare_pack(&headers,0,true)==0);
    assert(buf_aud.offset==288); /* headers do not consume audio or advance PTS */
    uint64_t expected=40000;
    for(unsigned int i=0;i<10000;++i) {
        unsigned int delta=i<2500?60000:i<5000?60060:i<7500?40000:66667;
        pack.timestamp+=delta;
        assert(mp4_prepare_pack(&pack,0,true)==1);
        assert(last_video_duration==delta);
        if(i==0) assert(last_audio_duration==72000);
        expected+=delta;
        assert(mp4_set_state(&a)==BUF_OK && mp4_set_state(&b)==BUF_OK);
    }
    assert(a.video_media_decode_time==expected);
    assert(a.video_media_decode_time==b.video_media_decode_time);
    mp4_capture_discontinuity();
    pack.timestamp=0;
    assert(mp4_prepare_pack(&pack,0,true)==1 && last_video_duration==66667);
    pack.timestamp=60000;
    assert(mp4_prepare_pack(&pack,0,true)==1 && last_video_duration==60000);
    hal_vidpack bad=pack;
    bad.nalu[1].offset=0;
    assert(mp4_prepare_pack(&bad,0,true)==-1);
    bad=pack; bad.nalu[0].length=UINT32_MAX;
    assert(mp4_prepare_pack(&bad,0,true)==-1);
    assert(mp4_ingest_audio((char*)audio,UINT32_MAX)==BUF_INCORRECT);
    free(access_unit.buf); free(buf_aud.buf); free(buf_header.buf);
    free(buf_mdat.buf); free(buf_moof.buf);
    puts("MP4 capture timing: 10000 frames, rational rates, multi-VCL, headers, shared consumers PASS");
    return 0;
}
