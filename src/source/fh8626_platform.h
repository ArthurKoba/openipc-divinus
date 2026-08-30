#pragma once

#include <stddef.h>

void fh8626_platform_apply_identity(void);
int fh8626_platform_write_json(char *buffer, size_t size,
    int rtsp_enabled, int mp4_enabled);
