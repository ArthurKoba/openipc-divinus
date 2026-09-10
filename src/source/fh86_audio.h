#ifndef FH86_AUDIO_H
#define FH86_AUDIO_H

#include "../hal/types.h"

typedef int (*fh86_audio_frame_cb)(hal_audframe *frame);

int fh86_audio_start(fh86_audio_frame_cb callback);
void fh86_audio_stop(void);
int fh86_audio_running(void);

#endif
