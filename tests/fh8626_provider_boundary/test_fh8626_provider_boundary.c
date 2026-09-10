#include "fh8626_provider.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void check_blockers(uint32_t blockers)
{
    assert(blockers & FH8626_BLOCKER_DETECTION);
    assert(blockers & FH8626_BLOCKER_DEVICE_MAP);
    assert(blockers & FH8626_BLOCKER_PIPELINE_OWNERSHIP);
    assert(blockers & FH8626_BLOCKER_FORCE_IDR);
    assert(blockers & FH8626_BLOCKER_RATE_CONTROL);
    assert(blockers == FH8626_PRODUCTION_BLOCKERS);
}

int main(int argc, char **argv)
{
    struct fh8626_provider_status status;

    assert(argc == 2);
    assert(fh8626_provider_get_status(NULL) < 0);
    assert(fh8626_provider_get_status(&status) == 0);
    check_blockers(status.blockers);
    assert(!status.production);
    assert(!fh8626_provider_production_ready());

    if (!strcmp(argv[1], "stub")) {
        assert(status.kind == FH8626_PROVIDER_STUB);
        assert(status.selectable);
        assert(!strcmp(status.name, "stub"));
    } else {
        assert(!strcmp(argv[1], "normal"));
        assert(status.kind == FH8626_PROVIDER_NONE);
        assert(!status.selectable);
        assert(!strcmp(status.name, "none"));
    }

    puts("fh8626_provider_boundary PASS");
    return 0;
}
