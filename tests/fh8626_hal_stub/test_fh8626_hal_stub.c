#include "fh8626_hal.h"

#include <assert.h>
#include <stdio.h>
#include <unistd.h>

char keepRunning = 1;

static unsigned delivered;

static int sink(char index, hal_vidstream *stream)
{
    assert(index == FH8626_NATIVE_CHANNEL);
    assert(stream && stream->count == 1u);
    assert(stream->pack && stream->pack->naluCnt == 3);
    assert(stream->pack->nalu[0].type == 7);
    assert(stream->pack->nalu[1].type == 8);
    assert(stream->pack->nalu[2].type == 5);
    delivered++;
    return 0;
}

int main(void)
{
    assert(fh8626_hal_stub_enabled());
    assert(fh8626_sdk_start(sink) == 0);
    usleep(130000);
    assert(fh8626_sdk_stop() == 0);
    assert(delivered >= 2u);
    assert(!fh8626_state[0].enable);
    puts("fh8626_hal_stub PASS");
    return 0;
}
