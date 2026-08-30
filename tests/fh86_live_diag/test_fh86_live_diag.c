#include "../../src/source/fh86_live_diag.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    struct fh86_live_diag diag;
    char json[1024];

    fh86_live_diag_reset(&diag, 1);
    assert(fh86_live_diag_write_json(&diag, 1000, json, sizeof(json)) > 0);
    assert(strstr(json, "\"state\":\"waiting\"") != NULL);
    assert(strstr(json, "\"last_frame_age_ms\":null") != NULL);

    fh86_live_diag_observe(&diag, 32, 40000, 1, 1, 1, 0, 2000);
    assert(fh86_live_diag_write_json(&diag, 2040, json, sizeof(json)) > 0);
    assert(strstr(json, "\"state\":\"streaming\"") != NULL);
    assert(strstr(json, "\"frames_received\":1") != NULL);
    assert(strstr(json, "\"frames_forwarded\":1") != NULL);
    assert(strstr(json, "\"tiny_frames\":1") != NULL);
    assert(strstr(json, "\"generation_changes\":1") != NULL);
    assert(strstr(json, "\"last_frame_bytes\":32") != NULL);

    assert(fh86_live_diag_write_json(&diag, 5001, json, sizeof(json)) > 0);
    assert(strstr(json, "\"state\":\"stalled\"") != NULL);

    fh86_live_diag_observe(&diag, 3664, 80000, 1, 0, 0, 1, 6000);
    assert(fh86_live_diag_write_json(&diag, 6000, json, sizeof(json)) > 0);
    assert(strstr(json, "\"pack_errors\":1") != NULL);
    assert(strstr(json, "\"frames_forwarded\":1") != NULL);
    assert(strstr(json, "\"last_frame_bytes\":3664") != NULL);

    fh86_live_diag_set_running(&diag, 0);
    assert(fh86_live_diag_write_json(&diag, 6000, json, sizeof(json)) > 0);
    assert(strstr(json, "\"state\":\"stopped\"") != NULL);

    puts("fh86_live_diag PASS");
    return 0;
}
