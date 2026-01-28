#include "asc_runtime.h"
#include "asclib.h"
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>



async(sleeper, uint64_t ms )
    // uint64_t val = arg(ms); // Optional: if you want a local copy
    printf("Sleeping for: %lu\n", arg(ms)); 
    await(asc_sleep(arg(ms)));
    printf("Woken up: %lu ms\n", arg(ms));
    task_return_void();
}

async(counter, int n; char label )
    for (int i = 0; i < arg(n); i++) {
        printf("%c: %d\n", arg(label), i);
        yield;
    }
    task_return_void();
}

async(main_task, uint64_t ms)

    printf("Racing two tasks\n");

    // task_spawn now correctly builds the task_entry_t
    task_handle h1 = task_spawn(sleeper, .ms = arg(ms));
    await(h1);
    await(task_spawn(counter, .n = 10, .label = 'x'));
    printf("Finished\n");
    task_return_void();
}

int main(int argc, char** argv) {
    // For main_task (no params), we use the old entry style or update it too
    uint64_t ms = (argc > 1)? atoi(argv[1]) : 2000;
    (void)ms;

    task__arg__constructor te = task_entry(main_task, int, ms); (void)te;

    worker_t* w = worker_create(1, task_entry(main_task, int, ms));

    worker_run(w);
    worker_await(w);

    printf("Returned!\n");
    worker_destroy(w);
    printf("End program!!\n");
    return 0;
}