#include "fh8626_provider.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    struct fh8626_provider_status status;

    assert(argc == 2);
    assert(fh8626_provider_get_status(NULL) < 0);
    assert(fh8626_provider_get_status(&status) == 0);
    assert(!status.production);
    assert(!fh8626_provider_production_ready());

    if (!strcmp(argv[1], "stub")) {
        assert(status.kind == FH8626_PROVIDER_STUB);
        assert(status.selectable);
        assert(!strcmp(status.name, "stub"));
        assert(status.blockers == FH8626_STUB_BLOCKERS);
    } else if (!strcmp(argv[1], "kernel")) {
        assert(status.kind == FH8626_PROVIDER_KERNEL);
        assert(status.selectable);
        assert(!strcmp(status.name, "kernel"));
        assert(status.blockers == FH8626_KERNEL_BLOCKERS);
        assert(!(status.blockers & FH8626_BLOCKER_SENSOR_VENDOR_PLUGIN));
        assert(!(status.blockers & FH8626_BLOCKER_ISP_PROFILE_DATA));
        assert(status.blockers & FH8626_BLOCKER_AUDIO_HARDWARE_ACCEPTANCE);
        assert(!(status.blockers & FH8626_BLOCKER_RUNTIME_RECONFIG));
        assert(!(status.blockers & FH8626_BLOCKER_SAME_BOOT_TEARDOWN));
        assert(status.blockers & FH8626_BLOCKER_HARDWARE_ACCEPTANCE);
    } else {
        assert(!strcmp(argv[1], "normal"));
        assert(status.kind == FH8626_PROVIDER_NONE);
        assert(!status.selectable);
        assert(!strcmp(status.name, "none"));
        assert(status.blockers == FH8626_BLOCKER_PROVIDER_UNAVAILABLE);
    }

    puts("fh8626_provider_boundary PASS");
    return 0;
}
