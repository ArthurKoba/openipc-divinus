#include "source/fh8626_webdiag.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void require_true(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

int main(void) {
    const char *html = fh8626_webdiag_html();

    require_true(html != NULL, "HTML pointer");
    require_true(strstr(html, "FH8626V100 live diagnostics") != NULL, "page identity");
    require_true(strstr(html, "/api/platform") != NULL, "platform API wired");
    require_true(strstr(html, "/api/fh86") != NULL, "FH86 API wired");
    require_true(strstr(html, "/api/live") != NULL, "live API wired");
    require_true(strstr(html, "Start browser preview") != NULL, "manual preview control");
    require_true(strstr(html, "rtsp://") != NULL, "RTSP URL builder");
    require_true(strstr(html, "unavailable") != NULL, "unavailable semantics visible");
    require_true(strlen(html) < 16384, "embedded page bounded");

    puts("fh8626_webdiag PASS");
    return 0;
}
