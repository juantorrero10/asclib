#define _GNU_SOURCE

#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
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

// ========================= TCP ===========================

typedef struct _ASCLIB_TCP_Listener_ {
    int fd;
    uint8_t ip_addr[4];
    uint16_t port;
} tcp_listener_t;

typedef struct _ASCLIB_TCP_Client_ {
    int fd;
    uint8_t server_ip_addr[4];
    uint16_t server_port;
} tcp_client_t;



decl_async(int, __internal_tcp_accept, tcp_listener_t* l);
decl_async(tcp_client_t*, __internal_tcp_connect, uint8_t* ip_addr; uint16_t port);

/*
static inline void str_addr(char* c16, uint8_t addr4[]) {
    sprintf(c16, "%hhu.%hhu.%hhu.%hhu", 
        addr4[0], addr4[1], addr4[2], addr4[3]);
}*/



tcp_listener_t* asc_tcp_bind(uint8_t* ip_addr, uint16_t port) {
    char* f = "NET::asc_tcp_bind()";

    int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (fd < 0) {
        goto __clean_fail;
    }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_port = htons(port)
    };

    memcpy(&address.sin_addr.s_addr, ip_addr, 4);

    if (bind(fd, (struct sockaddr*)&address, sizeof(address)) < 0)
        goto __clean_fail;

    if (listen(fd, SOMAXCONN) < 0)
        goto __clean_fail; 


    tcp_listener_t* ret = malloc(sizeof(tcp_listener_t));
    ret->fd = fd;
    memcpy(ret->ip_addr, ip_addr, 4);
    ret->port = port;

    return ret;


__clean_fail:
    if (fd >= 0) close(fd);
    asc_error(WARN_RETURNING_NULL, f, (any_t)(void*)"TCP Bind");
    return NULL;
}

async_(__internal_tcp_connect, uint8_t* ip_addr; uint16_t port) {
    uint8_t* ip_addr = arg(ip_addr);
    uint16_t port = arg(port);
    int fd = -1;
    if (!ip_addr) goto __clean_fail;
    worker_t* w = tl_worker;

    fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (fd < 0) goto __clean_fail;

    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port)
    };

    memcpy(&server_addr.sin_addr.s_addr, ip_addr, 4);


    int nodelay = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

    // Set waiting for writeable fd
    __claim_fd(fd, w->tasks.current, EPOLLOUT);
    int st = connect(fd, (struct sockaddr*)&server_addr, sizeof(server_addr));

    // Connection stablished
    if (st >= 0) {
__connection_success:
        __release_fd(fd);
        tcp_client_t* tcp = malloc(sizeof(tcp_client_t));
        tcp->fd = fd;
        tcp->server_port = port;
        memcpy(&tcp->server_ip_addr, ip_addr, 4);
        task_return(tcp_client_t*, tcp);
    }

    if (errno == EINPROGRESS || errno == EAGAIN || errno == EWOULDBLOCK) {
        __task_wait_on_fd(fd);
        st = 0;
        socklen_t sl = sizeof(st);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &st, &sl) < 0 || st != 0) {
            if (st != 0) errno = st;
            __release_fd(fd);
            goto __clean_fail;
        } else {
            goto __connection_success;
        }
    }

    else {
        __release_fd(fd);
        goto __clean_fail;
    }


__clean_fail:
    if (fd >= 0) close(fd);
    asc_error(WARN_RETURNING_NULL, "NET::tcp_connect", (any_t)(void*)"TCP Connect");
    end_return_void();
}


task_handle_t asc_tcp_connect(uint8_t* ip_addr, uint16_t port) {
    worker_t* w = tl_worker;
    ASSERT_CONTEXT(w, "NET::asc_tcp_connect()");
    return task_spawn_ex(__internal_tcp_connect,4096,1, .ip_addr = ip_addr, .port = port);

}

async_(__internal_tcp_accept, tcp_listener_t* l) {
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


task_handle_t asc_tcp_accept(tcp_listener_t* l) {
    worker_t* w = tl_worker;
    ASSERT_CONTEXT(w, "NET::asc_tcp_accept()");

    return task_spawn_ex(__internal_tcp_accept, 4096, 1, .l = l);
}

// ==================== UDP =============================

typedef struct _ASCLIB_UDP_Peer_ {
    uint8_t ip_addr[4];
    uint16_t port;
} udp_peer_t;

typedef struct _ASCLIB_UDP_Node_ {
    int fd;
    udp_peer_t bind_addr;
    // When calling ```asc_udp_target() -> connect()``` on a udp socket, it targets one specific address for sending/receiving
    // This can speed up certain kernel operations.
    int is_connected;
    // Target peer for operations, if is_connected=1 sendto and recvfrom will use this peer.       
    udp_peer_t target;
} udp_node_t;

decl_async(ssize_t, __internal_udp_recvfrom, udp_node_t* _socket; void* buff; size_t n; udp_peer_t* out_peer);
decl_async(ssize_t, __internal_udp_sendto, udp_node_t* _socket; void* buff; size_t n; udp_peer_t target_peer);

udp_node_t* asc_udp_bind(uint8_t* ip_addr, uint16_t port) {
    char* f = "NET::asc_udb_bind()";

    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (fd < 0) {
        goto __clean_fail;
    }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_port = htons(port)
    };
    
    memcpy(&address.sin_addr.s_addr, ip_addr, 4);


    if (bind(fd, (struct sockaddr*)&address, sizeof(address)) < 0)
        goto __clean_fail;


    udp_node_t* ret = malloc(sizeof(udp_node_t));
    ret->fd = fd;
    ret->bind_addr.port = port;
    memcpy(&ret->bind_addr.ip_addr, ip_addr, 4);

    return ret;


__clean_fail:
    if (fd >= 0) close(fd);
    asc_error(WARN_RETURNING_NULL, f, (any_t)(void*)"UDP Bind");
    return NULL;
}


udp_node_t* asc_udp_socket() {
    char* f = "NET::asc_udb_socket()";

    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (fd < 0) {
        goto __clean_fail;
    }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));


    udp_node_t* ret = malloc(sizeof(udp_node_t));
    ret->is_connected = 0;
    ret->fd = fd;

    return ret;


__clean_fail:
    if (fd >= 0) close(fd);
    asc_error(WARN_RETURNING_NULL, f, (any_t)(void*)"UDP Bind");
    return NULL;
}


void asc_udp_target(udp_node_t* client_socket, uint8_t* target_ip_addr, uint16_t target_port) {
    char* f = "NET::asc_udp_target()";
    if (!client_socket || client_socket->fd < 0 || !target_ip_addr) goto __clean_fail;

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(target_port)
    };
    memcpy(&addr.sin_addr, target_ip_addr, 4);
    
    int st = connect(client_socket->fd, (struct sockaddr*)&addr, sizeof(addr));
    if (st < 0) {
        goto __clean_fail;
    }

    client_socket->is_connected = 1;

    return;


__clean_fail:
    asc_io_error(IO_ERROR_INVALID_PARAMETERS, f, (any_t)(void*)"UDP Target", (any_t)0);
    return;
}


/**
 * @brief Receiving a UDP Packet.
 * @brief Spawns a task to waits until data is received.
 * @param out_peer [OUT] Source IP Address and port. 
 * @note  If calling on a "targeted" UDP node it will only receive from a singular address. See ```asc_udp_target()```.
 * @returns The task returns a ```ssize_t``` that is < 0 on failure and values >= 0 describe the length of the data.
 */
task_handle_t asc_recvfrom(udp_node_t* socket, void* buff, size_t n, udp_peer_t* out_peer) {
    worker_t* w = tl_worker;
    ASSERT_CONTEXT(w, "NET::asc_recvfrom()");
    return task_spawn_ex(__internal_udp_recvfrom, 4096, 1, ._socket = socket, .buff = buff, .n = n, .out_peer = out_peer);
}

/**
 * @brief Sending a UDP Packet.
 * @brief Spawns a task to waits until data is received.
 * @param target_peer Address and port to send the data to.
 * @note  If calling on a "targeted" UDP node, ```target_peer``` will be ignored and use the target inside the socket. See ```asc_udp_target()```.
 * @returns The task returns a ```ssize_t``` that is < 0 on failure and values >= 0 describe the length of the data.
 */
task_handle_t asc_sendto(udp_node_t* socket, void* buff, size_t n, udp_peer_t target_peer) {
    worker_t* w = tl_worker;
    ASSERT_CONTEXT(w, "NET::asc_recvfrom()");
    return task_spawn_ex(__internal_udp_sendto, 4096, 1, ._socket = socket, .buff = buff, .n = n, .target_peer = target_peer);
}



async_(__internal_udp_recvfrom, udp_node_t* _socket; void* buff; size_t n; udp_peer_t* out_peer) {
    worker_t* w = tl_worker;
    udp_node_t* socket = arg(_socket);
    void* buff = arg(buff);
    size_t n = arg(n);
    udp_peer_t* out_peer = arg(out_peer);

    if (!socket || !buff || n == 0 || socket->fd < 0) {
        errno = EINVAL;
        task_return(ssize_t, -1); 
    }

    struct sockaddr_in out_addr = {0};
    socklen_t sl = sizeof(out_addr);

    __claim_fd(socket->fd, w->tasks.current, EPOLLIN);
    for(;;) {
        ssize_t sz = (socket->is_connected)
        ? recv(socket->fd, buff, n, MSG_DONTWAIT)
        : recvfrom(socket->fd, buff, n, MSG_DONTWAIT, (struct sockaddr*)&out_addr, &sl);

        if (sz >= 0) {
            __release_fd(socket->fd);
            if (!socket->is_connected && out_peer) {
                out_peer->port = ntohs(out_addr.sin_port);
                memcpy(&out_peer->ip_addr, &out_addr.sin_addr, 4);
            }
            task_return(ssize_t, sz);
        }
        else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            __task_wait_on_fd(socket->fd);
            continue;
        }

        else {
            __release_fd(socket->fd);
            task_return(ssize_t, -1);
        }
    }


    end_return_void();
}

async_(__internal_udp_sendto, udp_node_t* _socket; void* buff; size_t n; udp_peer_t target_peer) {
    worker_t* w = tl_worker;
    udp_node_t* socket = arg(_socket);
    void* buff = arg(buff);
    size_t n = arg(n);
    udp_peer_t target_addr = arg(target_peer);

    if (!socket || !buff || n == 0 || socket->fd < 0) {
        errno = EINVAL;
        task_return(ssize_t, -1); 
    }
    struct sockaddr_in target = {.sin_family = AF_INET};
    if (!socket->is_connected) {
        target.sin_port = htons(target_addr.port);
        memcpy(&target.sin_addr, target_addr.ip_addr, 4);
    }

    __claim_fd(socket->fd, w->tasks.current, EPOLLOUT);
    for(;;) {
        ssize_t sz = (socket->is_connected)
        ? send(socket->fd, buff, n, MSG_DONTWAIT)
        : sendto(socket->fd, buff, n, MSG_DONTWAIT, (struct sockaddr*)&target, sizeof(target));
        if (sz >= 0) {
            __release_fd(socket->fd);
            task_return(ssize_t, sz);
        }
        else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            __task_wait_on_fd(socket->fd);
            continue;
        }
        else {
            __release_fd(socket->fd);
            task_return(ssize_t, -1);
        }
    }
    end_return_void();
}

