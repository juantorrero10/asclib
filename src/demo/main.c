#include <stdio.h>


#include "asclib/core.h"
#include "asclib/sync.h"


async(dummy) {
    await(asc_sleep(500));
    task_return_void();
}

async(dummy2) {
    await(asc_sleep(500));
    task_return(int, 42);
}

async(main_task) {

    task_handle_t t1 = task_spawn(dummy2);

    await(t1);

    await(asc_sleep(1000));

    int n = await_ret(int, t1);
    printf("n = %d\n", n);
    
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