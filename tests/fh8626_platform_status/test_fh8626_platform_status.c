#include "source/fh8626_platform.h"
#include "hal/globals.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *aud_thread;
void *isp_thread;
void *vid_thread;
char chnCount;
hal_chnstate *chnState;
char chip[16] = "unknown";
char family[32] = {0};
hal_platform plat = HAL_PLATFORM_UNK;
char sensor[16] = "unidentified";
int series;

static void require_true(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

int main(void) {
    char json[1024];
    int n;

    fh8626_platform_apply_identity();
    require_true(!strcmp(chip, "FH8626V100"), "chip identity");
    require_true(!strcmp(family, "fullhan-fh8626"), "family identity");
    require_true(!strcmp(sensor, "gc1054_mipi"), "sensor identity");

    n = fh8626_platform_write_json(json, sizeof(json), 1, 1);
    require_true(n > 0, "platform JSON builds");
    require_true(strstr(json, "\"encoded_h264\":\"available\"") != NULL,
        "H264 provider available");
    require_true(strstr(json, "\"temperature\":\"unavailable\"") != NULL,
        "temperature is explicit unavailable");
    require_true(strstr(json, "\"gpio\":\"unavailable\"") != NULL,
        "GPIO is explicit unavailable");
    require_true(strstr(json, "\"rtsp\":\"available\"") != NULL,
        "RTSP capability reflects config");
    require_true(strstr(json, "\"fmp4\":\"available\"") != NULL,
        "fMP4 capability reflects config");

    require_true(fh8626_platform_write_json(json, 8, 1, 1) < 0,
        "short output buffer rejected");

    puts("fh8626_platform_status PASS");
    return 0;
}
