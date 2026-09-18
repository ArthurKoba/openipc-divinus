#include "fh8626_provider.h"

#include <errno.h>

int fh8626_provider_get_status(struct fh8626_provider_status *status)
{
    if (!status)
        return -EINVAL;

#ifdef FH8626_NATIVE_STUB
    status->kind = FH8626_PROVIDER_STUB;
    status->name = "stub";
    status->blockers = FH8626_STUB_BLOCKERS;
    status->selectable = 1;
#elif defined(FH8626_NATIVE_KERNEL)
    status->kind = FH8626_PROVIDER_KERNEL;
    status->name = "kernel";
    status->blockers = FH8626_KERNEL_BLOCKERS;
    status->selectable = 1;
#else
    status->kind = FH8626_PROVIDER_NONE;
    status->name = "none";
    status->blockers = FH8626_BLOCKER_PROVIDER_UNAVAILABLE;
    status->selectable = 0;
#endif
    status->production = status->selectable && status->blockers == 0u;
    return 0;
}

int fh8626_provider_production_ready(void)
{
    struct fh8626_provider_status status;

    if (fh8626_provider_get_status(&status))
        return 0;
    return status.production && status.selectable && status.blockers == 0u;
}
