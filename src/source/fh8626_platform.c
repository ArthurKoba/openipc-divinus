#include "fh8626_platform.h"

#include "../hal/globals.h"

#include <stdio.h>
#include <string.h>

#define FH8626_CHIP "FH8626V100"
#define FH8626_FAMILY "fullhan-fh8626"
#define FH8626_SENSOR "gc1054_mipi"

void fh8626_platform_apply_identity(void) {
    snprintf(chip, sizeof(chip), "%s", FH8626_CHIP);
    snprintf(family, sizeof(family), "%s", FH8626_FAMILY);
    snprintf(sensor, sizeof(sensor), "%s", FH8626_SENSOR);
}

int fh8626_platform_write_json(char *buffer, size_t size,
    int rtsp_enabled, int mp4_enabled) {
    int written;

    if (!buffer || !size)
        return -1;

    written = snprintf(buffer, size,
        "{\"source\":\"fh86\",\"chip\":\"%s\",\"family\":\"%s\","
        "\"sensor\":\"%s\",\"providers\":{"
        "\"encoded_h264\":\"available\",\"rtsp\":\"%s\",\"fmp4\":\"%s\","
        "\"temperature\":\"unavailable\",\"gpio\":\"unavailable\","
        "\"jpeg\":\"unavailable\",\"mjpeg\":\"unavailable\","
        "\"audio\":\"unavailable\",\"osd\":\"unavailable\"},"
        "\"api\":{\"live\":\"/api/live\",\"source\":\"/api/fh86\",\"webdiag\":\"/fh8626\"}}",
        FH8626_CHIP, FH8626_FAMILY, FH8626_SENSOR,
        rtsp_enabled ? "available" : "disabled",
        mp4_enabled ? "available" : "disabled");

    if (written < 0 || (size_t)written >= size)
        return -1;
    return written;
}
