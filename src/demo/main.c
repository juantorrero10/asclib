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

    await(asc_send(fd, "___HELLO____WOR", 15));
    end_return_void();
}


async(main_task) {

    char a[10] = { 1 };
    char b[10] = { 0 };

    memset(b, 65, 10);

    struct iovec vecs[] = {
        {.iov_base = a, .iov_len = 10},
        {.iov_base = b, .iov_len = 10}
    };


    struct msghdr msg = {
        .msg_iov = vecs,
        .msg_iovlen = 2
    };

    int fds[2];

    socketpair(AF_UNIX, SOCK_STREAM, 0, fds);

    task_spawn(sender, .fd = fds[1]);
    clean_on_drop(fds[1]);

    ssize_t n = await_ret(ssize_t, asc_recvmsg(fds[0], &msg));
    if (n <= 0) {
        printf("some wrong %s\n", strerror(errno));
    } else {
        printf("Buff1: %.*s\nBuff2: %.*s\n", 10, a, ((int)n-10), b);
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