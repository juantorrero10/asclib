#include <string.h>
#include <stdio.h>

#include "asclib/core.h"
#include "asclib/env.h"
#include "asclib/sync.h"


ENV_DECLARE(int, KEY);
ENV_DECLARE_DYN(char, BUFF, free);


async(counter) {
    detach;
    for (size_t i = 0; i < 13; i++)
    {
        printf("i = %zu\n", i);
        await(asc_sleep(100));
    }
    end_return_void();
}

async(task) {
    
    printf("MY handle: %lx\n", (uint64_t)ENVGET_SELF().val);

    task_spawn(counter);
    await(asc_sleep(1000));

    end_return_void();
}

async(main_task) 
{
    ENVSET_BUFF(malloc(1024));
    strcpy(ENVGET_BUFF(), "Another string.");

    printf("[MAIN]: buffer before -> %s\n", ENVGET_BUFF());
    
    await(task_spawn(task));
    
    printf("[MAIN]: buffer after -> %s\n", ENVGET_BUFF());
    
    end_return_void();
}

int main() {

    worker_t* w = worker_create(1, task_entry(main_task));
    worker_run(w);
    worker_await_all(w);
    worker_destroy(w);

    // NO MEM LEAKS!!

    return 0;
}