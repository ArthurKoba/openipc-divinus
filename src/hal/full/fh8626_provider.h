#pragma once

#include <stdint.h>

/* OpenIPC's FH8626V100 target is ARM1176JZF-S (ARMv6KZ, soft-float EABI).
 * Select the native kernel provider automatically for that target so the
 * ordinary Buildroot Divinus package does not need a camera-specific flag.
 * Runtime activation is still gated by FH8626 identity and device nodes. */
#if !defined(FH8626_NATIVE_STUB) && !defined(FH8626_NATIVE_KERNEL) && \
    defined(__arm__) && defined(__ARM_ARCH_6ZK__)
#define FH8626_NATIVE_KERNEL 1
#endif

typedef enum {
    FH8626_PROVIDER_NONE = 0,
    FH8626_PROVIDER_STUB,
    FH8626_PROVIDER_KERNEL
} fh8626_provider_kind;

enum {
    FH8626_BLOCKER_PROVIDER_UNAVAILABLE = 1u << 0,
    FH8626_BLOCKER_STUB_BACKEND = 1u << 1,
    FH8626_BLOCKER_FORCE_IDR = 1u << 2,
    FH8626_BLOCKER_SAME_BOOT_TEARDOWN = 1u << 3,
    FH8626_BLOCKER_HARDWARE_ACCEPTANCE = 1u << 4
};

#define FH8626_KERNEL_BLOCKERS \
    (FH8626_BLOCKER_FORCE_IDR | FH8626_BLOCKER_SAME_BOOT_TEARDOWN | \
     FH8626_BLOCKER_HARDWARE_ACCEPTANCE)

#define FH8626_STUB_BLOCKERS \
    (FH8626_BLOCKER_STUB_BACKEND | FH8626_BLOCKER_HARDWARE_ACCEPTANCE)

struct fh8626_provider_status {
    fh8626_provider_kind kind;
    const char *name;
    uint32_t blockers;
    int selectable;
    int production;
};

int fh8626_provider_get_status(struct fh8626_provider_status *status);
int fh8626_provider_production_ready(void);
