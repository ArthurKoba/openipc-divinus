#pragma once

#include <stddef.h>

struct ptz_status {
    int available;
    int busy;
    int calibrated;
    int pan;
    int pan_min;
    int pan_max;
    int pan_home;
    int tilt;
    int tilt_min;
    int tilt_max;
    int tilt_home;
};

#define PTZ_MAX_PRESETS 8
struct ptz_preset {
    char token[32];
    int pan;
    int tilt;
};

int ptz_status_read(struct ptz_status *status);
int ptz_status_json(char *buffer, size_t size);
int ptz_move_relative(int pan, int tilt);
int ptz_move_absolute(int pan, int tilt);
int ptz_home(void);
int ptz_presets_read(struct ptz_preset *presets, size_t capacity);
int ptz_presets_json(char *buffer, size_t size);
int ptz_preset_set(const char *requested_token, char *token, size_t token_size);
int ptz_preset_goto(const char *token);
int ptz_preset_remove(const char *token);
