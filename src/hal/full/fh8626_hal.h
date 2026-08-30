#pragma once

#include "fh8626_native_adapter.h"
#include "fh8626_native_runtime.h"
#include "../types.h"

#define FH8626_VENC_CHN_NUM 1

extern hal_chnstate fh8626_state[FH8626_VENC_CHN_NUM];

int fh8626_hal_stub_enabled(void);
int fh8626_sdk_start(fh8626_video_sink sink);
int fh8626_sdk_stop(void);
