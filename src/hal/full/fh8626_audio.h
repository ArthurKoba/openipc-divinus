#pragma once

#include "../types.h"

typedef int (*fh8626_audio_frame_cb)(hal_audframe *frame);

int fh8626_audio_start(fh8626_audio_frame_cb callback);
void fh8626_audio_stop(void);
int fh8626_audio_running(void);
