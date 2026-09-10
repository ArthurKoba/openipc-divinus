#pragma once

#include <stdint.h>

typedef enum {
    FH8626_PROVIDER_NONE = 0,
    FH8626_PROVIDER_STUB,
    FH8626_PROVIDER_KERNEL
} fh8626_provider_kind;

enum {
    FH8626_BLOCKER_DETECTION = 1u << 0,
    FH8626_BLOCKER_DEVICE_MAP = 1u << 1,
    FH8626_BLOCKER_PIPELINE_OWNERSHIP = 1u << 2,
    FH8626_BLOCKER_FORCE_IDR = 1u << 3,
    FH8626_BLOCKER_RATE_CONTROL = 1u << 4
};

#define FH8626_PRODUCTION_BLOCKERS \
    (FH8626_BLOCKER_DETECTION | FH8626_BLOCKER_DEVICE_MAP | \
     FH8626_BLOCKER_PIPELINE_OWNERSHIP | FH8626_BLOCKER_FORCE_IDR | \
     FH8626_BLOCKER_RATE_CONTROL)

struct fh8626_provider_status {
    fh8626_provider_kind kind;
    const char *name;
    uint32_t blockers;
    int selectable;
    int production;
};

int fh8626_provider_get_status(struct fh8626_provider_status *status);
int fh8626_provider_production_ready(void);
