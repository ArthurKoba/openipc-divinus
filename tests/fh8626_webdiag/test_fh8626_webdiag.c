#include "source/fh8626_webdiag.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void require_true(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

int main(void)
{
    const char *html = fh8626_webdiag_html();

    require_true(html != NULL, "HTML pointer");
    require_true(strstr(html, "FH8626V100 live diagnostics") != NULL,
        "page identity");
    require_true(strstr(html, "/api/platform") != NULL,
        "platform API wired");
    require_true(strstr(html, "/api/fh86") != NULL,
        "FH86 API wired");
    require_true(strstr(html, "/api/live") != NULL,
        "live API wired");
    require_true(strstr(html, "id=\"start\" type=\"button\" disabled") != NULL,
        "preview starts fail-closed");
    require_true(strstr(html, "refreshBusy") != NULL,
        "poll overlap guard present");
    require_true(strstr(html, "document.execCommand('copy')") != NULL,
        "HTTP clipboard fallback present");
    require_true(strstr(html, "setRtsp") != NULL,
        "RTSP capability gates UI");
    require_true(strstr(html, "rtsp.path||'/'") != NULL,
        "RTSP capability path supported");
    require_true(strstr(html, "setRaw") != NULL,
        "raw H264 capability gates UI");
    require_true(strstr(html, "stopPreview") != NULL,
        "preview stops when capability disappears");
    require_true(strstr(html, "browser preview failed; use RTSP/raw H.264") != NULL,
        "preview play failure is surfaced");
    require_true(strstr(html, "fMP4 unavailable; use RTSP/raw H.264") != NULL,
        "unavailable transport guidance visible");
    require_true(strlen(html) < 16384,
        "embedded page bounded");

    puts("fh8626_webdiag_v11 PASS");
    return 0;
}
