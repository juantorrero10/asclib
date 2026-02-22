#ifndef ASCLIB_NET_TCP_H_
#define ASCLIB_NET_TCP_H_

#include <stdint.h>
#include <sys/socket.h>

typedef struct _AL_TH_ task_handle_t;

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

task_handle_t asc_tcp_accept(tcp_listener_t* l);

/**
 * @brief Create a TCP socket and make a server.
 */
tcp_listener_t* asc_tcp_bind(uint8_t ip_addr[], uint16_t port);

/**
 * @brief TCP client connect.
 * @brief Spawns a task that waits until connection success.
 * @returns the task returns a ```tcp_client_t*``` that is cleaned up by the caller. 
 */
task_handle_t asc_tcp_connect(uint8_t ip_addr[], uint16_t port);


/**
 * @brief Send a TCP packet.
 * @brief Spawns a task that waits until data is sent.
 * @param buff buffer containing the message to be sent.
 * @returns the task returns a ```ssize_t``` that is < 0 on failure and values >= 0 describe the length of the data. 
 */
task_handle_t asc_send(int fd, void* buff, size_t __n);

/**
 * @brief Send a TCP packet.
 * @brief Spawns a task that waits until some data is recv.
 * @param buff buffer that will contain the received message.
 * @returns the task returns a ```ssize_t``` that is < 0 on failure and values >= 0 describe the length of the data. 
 */
task_handle_t asc_recv(int fd, void* buff, size_t __n);

/**
 * @brief Vectored counterpart to ```asc_recv()```
 */
task_handle_t asc_recvmsg(int fd, struct msghdr *msg);

/**
 * @brief Vectored counterpart to ```asc_send()```
 */
task_handle_t asc_sendmsg(int fd, const struct msghdr *msg);

#endif // DEMO_ASCLIB_NET_H_