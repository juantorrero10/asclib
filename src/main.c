#include "asclib.h"
#include <stdio.h>

TASK(counter) {
    int starting_num = task_arg(int);

    for (int i = 1; i <= 10; i++)
    {
        printf("%d\n", starting_num + i * 5);
        task_yield();
    }

    task_return_void();
}



int main() {
    

    worker_t* w = worker_create(5, 
        task_entry(counter, int, -4),
        task_entry(counter, int, -3),
        task_entry(counter, int, -2),
        task_entry(counter, int, -1),
        task_entry(counter, int, 0)
    );

    worker_run(w);
    worker_await_all(w);
    worker_destroy(w);
    
}