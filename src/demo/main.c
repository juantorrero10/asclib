#include <stdio.h>


#include "asclib/core.h"
#include "asclib/sync.h"

    
async(sleeper, uint64_t ms; const char* label )
    // uint64_t val = arg(ms); // Optional: if you want a local copy
    printf("[%s]: Sleeping for: %lu\n", arg(label), arg(ms)); 
    await(asc_sleep(arg(ms)));
    printf("[%s]: Woken up: %lu ms\n", arg(label), arg(ms));
    task_return(uint64_t, arg(ms));


async(main_task, int value)

    printf("Racing two tasks, value = %d\n", arg(value));

    // task_spawn now correctly builds the task_entry_t
    task_handle h1 = task_spawn(sleeper, .ms = 200, .label = "Long sleeper");
    task_handle h2 = task_spawn(sleeper, .ms = 100, .label = "Short sleeper");
    
    uint64_t ms = (uint64_t)await_any(h1, h2).select();

    printf("Finished in: %lums\n", ms);
    task_return_void();






int main() {
    // For main_task (no params), we use the old entry style or update it too

    worker_t* w = worker_create(1, task_entry(main_task, int, 2));

    worker_run(w);
    worker_await(w);

    printf("Returned!\n");
    worker_destroy(w);
    printf("End program!!\n");
    return 0;
}