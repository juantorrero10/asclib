
#include <stdio.h>
#include "asclib/core.h"
#include "asclib/sync.h"
#include "asclib/io.h"
#include "asclib/cleanup.h"

#include <string.h>

async(heap_user) {

    void* heap5 = clean_on_drop(malloc(1024)); (void)heap5;
    void* heap4 = clean_on_drop(malloc(1024)); (void)heap4;
    void* heap3 = clean_on_drop(malloc(1024)); (void)heap3;
    void* heap2 = clean_on_drop(malloc(1024)); (void)heap2;
    void* heap1 = clean_on_drop(malloc(1024)); (void)heap1;

    await(asc_sleep(5000));

    end_return_void();
}

async(main_task) {

    task_handle_t t1 = task_spawn(heap_user);
    yield;
    drop_task(t1);


    end_return_void();
}

int main() {
    // For main_task (no params), we use the old entry style or update it too
    //await(task_spawn(main_task));
    worker_t* w = worker_create(1, task_entry(main_task));

    worker_run(w);
    worker_await_all(w);
    printf("Returned!\n");
    worker_destroy(w);
    return 0;
}