#include <stdio.h>


#include "asclib/core.h"
#include "asclib/sync.h"
#include "asclib/io.h"

async(dummy) {
    await(asc_sleep(500));
    task_return_void();
}

async(main_task) {

    task_handle_t t1 = task_spawn(dummy);

    await(asc_sleep(1000));

    uint64_t n = await_ret(uint64_t, t1);

    printf("n = %lu\n", n);

    task_return_void();
}

int main() {
    // For main_task (no params), we use the old entry style or update it too

    worker_t* w = worker_create(1, task_entry(main_task));

    worker_run(w);
    printf("Returned!\n");
    worker_await_all(w);
    worker_destroy(w);
    printf("End program!!\n");
    return 0;
}