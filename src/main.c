#include "asclib.h"
#include "asc_runtime.h"
#include <stdio.h>


async(reader) {
    ignore_arg();

    char buff[1024];
    printf(">>>: ");
    ssize_t s = await_ret(ssize_t, asc_read(0, buff, 1024));

    printf("Read %zd bytes -> %s", s, buff);

    task_return_void();    
}

async(writer) {
    char* file = task_arg(char*);

    int fd = asc_open(file, O_WRONLY | O_APPEND, 0);
    if (fd == -1) task_return_void();

    char buff[] = "This is some data!\n";

    printf("[Writer] trying to write: %s", buff);
    ssize_t written = await_ret(ssize_t, asc_write(fd, buff, 19));
    printf("[Writter] Written %zd bytes\n", written);


    task_return(written, ssize_t);
}

async(counter) {
    ignore_arg();

    for (size_t i = 0; i < 10; i++)
    {
        printf("i = %zu\n", i);
        yield;
    }
    

    task_return_void();
}


async(father) {
    ignore_arg();

    task_handle timeout = asc_sleep(2000);
    printf("Hello from father\n");

    
    await(task_new(reader, int, 0));
    await(task_new(reader, int, 0));
    
    await(timeout);

    printf("Timed out\n");
    task_return_void();

}




int main() {


    worker_t* w = worker_create(2, 
            task_entry(father, void*, 0),
            task_entry(counter, void*, 0)
        );
    
    worker_run(w);
    worker_await(w);

    printf("Returned!\n");

    worker_destroy(w);

    printf("End program!!\n");
}