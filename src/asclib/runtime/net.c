#define _GNU_SOURCE

#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>

#define __DEBUG
#include <log.h>

#include <user_lib.h>
#include <ascerr.h>
#include <dispatcher.h>
#include <runtime.h>

typedef struct _ASCLIB_TCP_Listener_ {
    int fd;
    uint8_t ip_addr[4];
    uint16_t port;
} tcp_listerner_t;

#define TCP_QUEUE_AMOUNT 128

decl_async(int, __internal_tcp_accept, tcp_listerner_t* l);

static inline void str_addr(char* c16, uint8_t addr4[]) {
    sprintf(c16, "%hhu.%hhu.%hhu.%hhu", 
        addr4[0], addr4[1], addr4[2], addr4[3]);
}



tcp_listerner_t* asc_tcp_bind(uint8_t* ip_addr, uint16_t port) {
    char* f = "NET::asc_tcp_bind()";

    int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (fd < 0) {
        goto __clean_fail;
    }

    char addr[16]; // 15 + '\0'
    str_addr(addr, ip_addr);

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));

    address.sin_family = AF_INET;
    address.sin_port = htons(port);

    if (inet_aton(addr, &address.sin_addr) == 0)
        goto __clean_fail;

    if (bind(fd, (struct sockaddr*)&address, sizeof(address)) < 0)
        goto __clean_fail;

    if (listen(fd, TCP_QUEUE_AMOUNT) < 0)
        goto __clean_fail; 


    tcp_listerner_t* ret = malloc(sizeof(tcp_listerner_t));
    ret->fd = fd;
    memcpy(ret->ip_addr, ip_addr, 4);
    ret->port = port;

    return ret;


__clean_fail:
    if (fd >= 0) close(fd);
    asc_error(WARN_RETURNING_NULL, f, (any_t)(void*)"TCP Bind");
    return NULL;
}

async_(__internal_tcp_accept, tcp_listerner_t* l) {
    struct sockaddr_in client;
    socklen_t clen = sizeof(client);
    worker_t* w = tl_worker;
    __claim_fd(arg(l)->fd, w->tasks.current, EPOLLIN);
    while(1) {
        int client_fd = accept4(arg(l)->fd, (struct sockaddr*)&client, &clen, SOCK_NONBLOCK | SOCK_CLOEXEC);

        // Instant connecction
        if (client_fd >= 0) {
            __release_fd(arg(l)->fd);
            task_return(int, client_fd); 
        }

        // No clients -> lazy wait
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            __task_wait_on_fd(arg(l)->fd);
            yield;
            continue;
        }

        __release_fd(arg(l)->fd);
        task_return(int, -1);
    }
    
    

    end_return_void();
}


task_handle_t asc_tcp_accept(tcp_listerner_t* l) {
    worker_t* w = tl_worker;
    ASSERT_CONTEXT(w, "NET::asc_tcp_accept()");

    return task_spawn_ex(__internal_tcp_accept, 4096, 1, .l = l);
}