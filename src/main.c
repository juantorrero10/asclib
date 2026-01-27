#include "asclib.h"
#include "asc_runtime.h"
#include "unistd.h"
#include <stdio.h>

typedef struct {
    int a;
    int b;
    int c;
    double d;
} some_info;


TASK(children) {
    ignore_arg();
    printf("Heey\n");
    await(asc_sleep(task_arg(uint64_t)));
    printf("Ended execution!!\n");
    task_return(42, int);
}

TASK(father) {
    ignore_arg();

    int a = 0;
    task_handle h1 = task_new(children, uint64_t, 1000); (void)h1;
    

    printf("Hello from father\n");

    a = await_ret(int, h1);
    //a = await(int, h2);

    printf("Children retuned: %d\n", a);

    some_info i = {0, 1, 2, 6.6f };

    task_return( i, some_info );
}

TASK(counter) {
    ignore_arg();

    for (size_t i = 0; i < 10; i++)
    {
        printf("i = %zu\n", i);
        yield;
    }
    

    task_return_void();
}



int main() {


    worker_t* w = worker_create(2, 
            task_entry(father, void*, 0),
            task_entry(counter, void*, 0)
        );
    
    worker_run(w);
    some_info i =  task_recover_from_worker(worker_await(w), 0, some_info);

    printf("a: %d\n", i.a);
    printf("b: %d\n", i.b);
    printf("c: %d\n", i.c);
    printf("d: %lf\n", i.d);

    worker_await_all(w);
    worker_destroy(w);
    
}