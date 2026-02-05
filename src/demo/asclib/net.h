#ifndef DEMO_ASCLIB_NET_H_
#define DEMO_ASCLIB_NET_H_

#include <stdint.h>
#include <sys/socket.h>

typedef struct _AL_TH_ task_handle_t;

typedef struct _ASCLIB_TCP_Listener_ {
    int fd;
    uint8_t ip_addr[4];
    uint16_t port;
} tcp_listerner_t;

tcp_listerner_t* asc_tcp_bind(uint8_t ip_addr[], uint16_t port);
task_handle_t asc_tcp_accept(tcp_listerner_t* l);

task_handle_t asc_read(int fd, void* buff, size_t __n);
task_handle_t asc_recv(int fd, void* buff, size_t __n);

task_handle_t asc_recvmsg(int fd, struct msghdr *msg);
task_handle_t asc_sendmsg(int fd, const struct msghdr *msg);

#endif // DEMO_ASCLIB_NET_H_