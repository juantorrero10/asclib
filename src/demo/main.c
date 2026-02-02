#include <string.h>
#include <stdio.h>
#include <errno.h>

#include "asclib/core.h"
#include "asclib/io.h"
#include "asclib/net.h"
#include "asclib/cleanup.h"
#include "asclib/env.h"

#define PORT 9999


async(handle_client, int client_fd) {
    detach; // Run in background
    int client_fd = arg(client_fd); clean_on_drop(client_fd); // Auto-close

    char buf[1024];
    char* msg = "Hello from C!\n\0";
    while (1) {
        // We'll build asc_read next
        ssize_t n = await_ret(ssize_t, asc_read(client_fd, buf, sizeof(buf)));
        if (n <= 0) break;
        printf("[+]: New connection -> FD:%d\n", client_fd);
        
        await(asc_write(client_fd, msg, strlen(msg)));
        printf("[-]: Closed connection\n");
    }
    end_return_void();
}

async(timeout, task_handle_t th) {
    detach;
    await(asc_sleep(5000));
    drop_task(arg(th));
    end_return_void();
}

async(main_server) {
    task_spawn(timeout, .th = ENVGET_SELF());
    tcp_listerner_t* listener = clean_on_drop(asc_tcp_bind((uint8_t[]){127, 0, 0, 1}, PORT));
    if (!listener) {
        printf("Failed to bind -> %s!\n", strerror(errno));
        task_return_void();
    }

    printf("Listening on port %hu...\n", PORT);

    while (1) {
        int client_fd = await_ret(int, asc_tcp_accept(listener));
        if (client_fd >= 0) {
            task_spawn(handle_client, .client_fd = client_fd);
        }
    }
    end_return_void();
}


int main() {

    worker_t* w = worker_create(1, task_entry(main_server));
    worker_run(w);
    worker_await_all(w);
    worker_destroy(w);

    // NO MEM LEAKS!!

    return 0;
}