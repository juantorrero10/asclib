#ifndef ASCLIB_NET_UDP_H_
#define ASCLIB_NET_UDP_H_

#include <stdint.h>
#include <stdio.h>

typedef struct _AL_TH_ task_handle_t;
// This macro is just a documentation tool for functions that spawn tasks.
// Since they have to return task_handle_t it opaques the task real return type
#define async_task(type) task_handle_t

#define peer_from(_addr, _port) ({\
    udp_peer_t __ret = {0};\
    memcpy(&__ret.ip_addr, (_addr), 4);\
    __ret.port = (_port);\
    __ret;\
})\

/**
 * @brief IP Address and port pair.
 */
typedef struct _ASCLIB_UDP_Peer_ {
    uint8_t ip_addr[4];
    uint16_t port;
} udp_peer_t;

/**
 * @brief Description of a UDP socket.
 */
typedef struct _ASCLIB_UDP_Node_ {
    int fd;
    udp_peer_t bind_addr;
    // When calling ```asc_udp_target() -> connect()``` on a udp socket, it targets one specific address for sending/receiving
    // This can speed up certain kernel operations.
    int is_connected;
    // Target peer for operations, if is_connected=1 sendto and recvfrom will use this peer.       
    udp_peer_t target;
} udp_node_t;

/**
 * @brief create an bind a local server udp socket ready to send and receive packets.
 * 
 */
udp_node_t* asc_udp_bind(uint8_t local_ip_addr[], uint16_t local_port);


/** 
 * @brief   Create a udp client socket.
 * @returns A UDP Socket descriptor or a "node".
*/
udp_node_t* asc_udp_socket();

/**
 * @brief Filter a udp socket so it only "cares" about a target addr and port.
 * @brief Internally,  ``` connect()``` is called which would be useless on udp except for this very functionality.
 */
void asc_udp_target(udp_node_t* client_socket, uint8_t target_ip_addr[], uint16_t target_port);


/**
 * @brief Receiving a UDP Packet.
 * @brief Spawns a task to waits until data is received.
 * @param out_peer [OUT] Source IP Address and port. 
 * @note  If calling on a "targeted" UDP node it will only receive from a singular address. See ```asc_udp_target()```.
 * @returns The task returns a ```ssize_t``` that is < 0 on failure and values >= 0 describe the length of the data.
 */
async_task(ssize_t) asc_recvfrom(udp_node_t* socket, void* buff, size_t n, udp_peer_t* out_peer);

/**
 * @brief Sending a UDP Packet.
 * @brief Spawns a task to waits until data is received.
 * @param target_peer Address and port to send the data to.
 * @note  If calling on a "targeted" UDP node, ```target_peer``` will be ignored and use the target inside the socket. See ```asc_udp_target()```.
 * @returns The task returns a ```ssize_t``` that is < 0 on failure and values >= 0 describe the length of the data.
 */
async_task(ssize_t) asc_sendto(udp_node_t* socket, void* buff, size_t n, udp_peer_t target_peer);

static void asc_peer_tostring(udp_peer_t* peer, char* buff, size_t n) {
    if (n < 24) return;
    sprintf(buff, "%hhu.%hhu.%hhu.%hhu:%hu", 
        peer->ip_addr[0],
        peer->ip_addr[1],
        peer->ip_addr[2],
        peer->ip_addr[3],
        peer->port
    );
}



#endif // ASCLIB_NET_UDP_H_