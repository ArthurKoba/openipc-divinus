#include "fh8626_hal.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

char keepRunning = 1;

static unsigned delivered;
static int fail_sink;

static int sink(char index, hal_vidstream *stream)
{
    assert(index == FH8626_NATIVE_CHANNEL);
    assert(stream && stream->count == 1u);
    assert(stream->pack && stream->pack->naluCnt == 3);
    assert(stream->pack->nalu[0].type == 7);
    assert(stream->pack->nalu[1].type == 8);
    assert(stream->pack->nalu[2].type == 5);
    delivered++;
    return fail_sink ? -EIO : 0;
}

static void clear_profile(void)
{
    unsetenv("FH8626_STUB_WRAP");
    unsetenv("FH8626_STUB_EAGAIN_EVERY");
}

static void run_success_case(int wrap, unsigned eagain_every)
{
    struct fh8626_stub_stats stats;
    char eagain[16];

    clear_profile();
    if (wrap)
        assert(setenv("FH8626_STUB_WRAP", "1", 1) == 0);
    if (eagain_every) {
        snprintf(eagain, sizeof(eagain), "%u", eagain_every);
        assert(setenv("FH8626_STUB_EAGAIN_EVERY", eagain, 1) == 0);
    }

    delivered = 0;
    fail_sink = 0;
    assert(fh8626_sdk_start(sink) == 0);
    assert(fh8626_sdk_start(sink) == -EBUSY);
    usleep(220000);
    assert(fh8626_sdk_stop() == 0);
    assert(fh8626_hal_stub_get_stats(&stats) == 0);
    assert(delivered >= 2u);
    assert(stats.frames_delivered == delivered);
    assert(stats.step_calls == delivered);
    assert(stats.media_calls >= stats.step_calls);
    assert(stats.leases_started == stats.leases_released);
    assert(stats.life_state == FH8626_LIFE_COLD);
    assert(!fh8626_state[0].enable);
}

static void run_sink_failure_case(void)
{
    struct fh8626_stub_stats stats;

    clear_profile();
    delivered = 0;
    fail_sink = 1;
    assert(fh8626_sdk_start(sink) == 0);
    usleep(100000);
    assert(fh8626_sdk_stop() == 0);
    assert(fh8626_hal_stub_get_stats(&stats) == 0);
    assert(delivered == 1u);
    assert(stats.frames_delivered == 0u);
    assert(stats.sink_errors == 1u);
    assert(stats.step_calls == 1u);
    assert(stats.leases_started == stats.leases_released);
    assert(stats.life_state == FH8626_LIFE_COLD);
}

int main(void)
{
    assert(fh8626_hal_stub_enabled());
    assert(fh8626_sdk_start(NULL) == -EINVAL);

    run_success_case(0, 0);
    run_success_case(1, 0);
    run_success_case(0, 2);
    run_success_case(1, 3);
    run_sink_failure_case();
    clear_profile();

    puts("fh8626_hal_stub PASS");
    return 0;
}
