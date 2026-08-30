#pragma once

#include <stddef.h>

int fh86_divinus_start(void);
void fh86_divinus_stop(void);
int fh86_divinus_write_status_json(char *buffer, size_t size);
