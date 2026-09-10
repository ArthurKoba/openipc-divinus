#define _POSIX_C_SOURCE 200809L
#include "ptz.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main(void) {
    char directory[] = "/tmp/divinus-ptz-test-XXXXXX";
    char helper[256], log_path[256], presets_path[256], line[128], json[768];
    struct ptz_status status;
    struct ptz_preset presets[PTZ_MAX_PRESETS];
    char token[32];
    FILE *file;

    assert(mkdtemp(directory));
    snprintf(helper, sizeof(helper), "%s/helper", directory);
    snprintf(log_path, sizeof(log_path), "%s/log", directory);
    snprintf(presets_path, sizeof(presets_path), "%s/presets", directory);
    file = fopen(helper, "w");
    assert(file);
    fputs("#!/bin/sh\n"
          "if [ \"$1\" = status ]; then\n"
          " echo 'busy=0 calibrated=1 pan=514 range=0,1028,514 tilt=200 range=0,250,200 state=/tmp/state'\n"
          " exit 0\n"
          "fi\n"
          "printf '%s %s %s\\n' \"$1\" \"$2\" \"$3\" >\"$DIVINUS_PTZ_TEST_LOG\"\n"
          "[ \"$2\" = 999 ] && exit 2\n"
          "exit 0\n", file);
    assert(!fclose(file));
    assert(!chmod(helper, 0755));
    assert(!setenv("DIVINUS_PTZ_HELPER", helper, 1));
    assert(!setenv("DIVINUS_PTZ_TEST_LOG", log_path, 1));
    assert(!setenv("DIVINUS_PTZ_PRESETS", presets_path, 1));

    assert(!ptz_status_read(&status));
    assert(status.available && status.calibrated && !status.busy);
    assert(status.pan == 514 && status.pan_max == 1028);
    assert(status.tilt == 200 && status.tilt_max == 250);
    assert(ptz_status_json(json, sizeof(json)) > 0);
    assert(strstr(json, "\"pan\":514"));

    assert(!ptz_move_relative(-32, 16));
    file = fopen(log_path, "r");
    assert(file && fgets(line, sizeof(line), file));
    fclose(file);
    assert(!strcmp(line, "move -32 16\n"));
    assert(!ptz_move_absolute(600, 100));
    assert(!ptz_home());
    assert(ptz_move_relative(999, 0) < 0);

    assert(!ptz_preset_set("door", token, sizeof(token)));
    assert(!strcmp(token, "door"));
    assert(ptz_presets_read(presets, PTZ_MAX_PRESETS) == 1);
    assert(ptz_presets_json(json, sizeof(json)) > 0);
    assert(strstr(json, "\"token\":\"door\""));
    assert(!strcmp(presets[0].token, "door"));
    assert(presets[0].pan == 514 && presets[0].tilt == 200);
    assert(!ptz_preset_goto("door"));
    assert(!ptz_preset_remove("door"));
    assert(ptz_presets_read(presets, PTZ_MAX_PRESETS) == 0);
    assert(ptz_preset_goto("door") < 0);

    unlink(presets_path);
    unlink(log_path);
    unlink(helper);
    rmdir(directory);
    puts("Divinus PTZ provider tests: PASS");
    return 0;
}
