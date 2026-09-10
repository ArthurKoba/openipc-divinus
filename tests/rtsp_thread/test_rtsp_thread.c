#include "../../src/rtsp/thread.h"

#include <assert.h>
#include <stdio.h>

static void *test_worker(void *opaque) {
    thread_handle thread = opaque;

    thread_sync_init(thread);
    thread_sync_cleanup(thread);
    return THREAD_SUCCESS;
}

int main(void) {
    threadpool_handle pool;
    thread_handle thread;

    pool = threadpool_create(NULL);
    assert(pool != NULL);

    thread = create_base_thread(pool, "rtspTest", test_worker, 1, NULL);
    assert(thread != NULL);

    assert(threadpool_start(pool) == SUCCESS);
    assert(threadpool_join(pool) == SUCCESS);

    threadpool_delete(pool);

    puts("rtsp_thread PASS");
    return 0;
}
