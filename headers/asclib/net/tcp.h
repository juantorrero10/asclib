#ifndef ASCLIB_NET_TCP_H_
#define ASCLIB_NET_TCP_H_

#include <stdint.h>
#include <sys/socket.h>

#ifndef ASCLIB_CORE_H_
typedef struct _AL_TH_ task_handle_t;

// This macro is just a documentation tool for functions that spawn tasks.
// Since they have to return task_handle_t it opaques the task real return type
#define async_task(type) task_handle_t
#endif


/**
 * @brief Descriptor of a listening TCP socket.
 */
typedef struct _ASCLIB_TCP_Listener_ {
    int fd;
    uint8_t ip_addr[4];
    uint16_t port;
} tcp_listener_t;

/**
 * @brief Descriptor of a connection to a TCP server. 
 */
typedef struct _ASCLIB_TCP_Client_ {
    int fd;
    uint8_t server_ip_addr[4];
    uint16_t server_port;
} tcp_client_t;

async_task(int) asc_tcp_accept(tcp_listener_t* l);

/**
 * @brief Create a TCP socket and make a server.
 */
tcp_listener_t* asc_tcp_bind(uint8_t ip_addr[], uint16_t port);

/**
 * @brief TCP client connect.
 * @brief Spawns a task that waits until connection success.
 * @returns the task returns a ```tcp_client_t*``` that is cleaned up by the caller. 
 */
async_task(tcp_client_t*) asc_tcp_connect(uint8_t ip_addr[], uint16_t port);


/**
 * @brief Send a TCP packet.
 * @brief Spawns a task that waits until data is sent.
 * @param buff buffer containing the message to be sent.
 * @returns the task returns a ```ssize_t``` that is < 0 on failure and values >= 0 describe the length of the data. 
 */
async_task(ssize_t) asc_send(int fd, void* buff, size_t __n);

/**
 * @brief Send a TCP packet.
 * @brief Spawns a task that waits until some data is recv.
 * @param buff buffer that will contain the received message.
 * @returns the task returns a ```ssize_t``` that is < 0 on failure and values >= 0 describe the length of the data. 
 */
async_task(ssize_t) asc_recv(int fd, void* buff, size_t __n);

/**
 * @brief Vectored counterpart to ```asc_recv()```
 */
async_task(ssize_t) asc_recvmsg(int fd, struct msghdr *msg);

/**
 * @brief Vectored counterpart to ```asc_send()```
 */
async_task(ssize_t) asc_sendmsg(int fd, const struct msghdr *msg);

#endif // DEMO_ASCLIB_NET_H_