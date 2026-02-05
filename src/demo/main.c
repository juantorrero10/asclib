#include <string.h>
#include <stdio.h>
#include <errno.h>

#include "asclib/core.h"
#include "asclib/io.h"
#include "asclib/net.h"
#include "asclib/sync.h"
#include "asclib/cleanup.h"

async(sender, int fd) {
    detach;
    int fd = clean_on_drop(arg(fd));
    asc_sleep(200);

    await(asc_write(fd, "___HELLO____WORLD___", 20));
    end_return_void();
}

/* async(counter) {
    for (size_t i = 0; i < 10; i++)
    {
        printf("i = %zu\n", i);
        yield;
    }
    end_return_void();
}
 */

async(main_task) {

    char a[20] = { 0 };
    char b[10] = { 0 };

    /*
    struct iovec vec[2] = {
        {.iov_base = a, .iov_len = 10},
        {.iov_base = b, .iov_len = 10},
    };*/

    int fds[2];

    socketpair(AF_UNIX, SOCK_STREAM, 0, fds);

    task_spawn(sender, .fd = fds[1]);
    //task_spawn(counter);
    ssize_t n = await_ret(ssize_t, asc_read(fds[0], a, 20));
    //ssize_t n = await_ret(ssize_t, asc_readv(fds[0], vec, 2));
    if (n <= 0) {
        printf("some wrong %s\n", strerror(errno));
    } else {
        printf("Buff1: %s\nBuff2: %s\n", a, b);
    }

    end_return_void();
}


int main() {

    worker_t* w = worker_create(1, task_entry(main_task));
    worker_run(w);
    worker_await_all(w);
    printf("Returned!\n");
    worker_destroy(w);
    return 0;
}