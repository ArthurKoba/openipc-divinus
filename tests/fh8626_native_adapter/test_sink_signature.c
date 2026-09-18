#include "../../src/hal/full/fh8626_native_adapter.h"

static int test_sink(char index, hal_vidstream *stream)
{
    (void)index;
    (void)stream;
    return 0;
}

void fh8626_native_adapter_sink_signature_check(void)
{
    fh8626_video_sink sink = test_sink;
    (void)sink;
}
