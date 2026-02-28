#ifndef CLEANUP_H_
#define CLEANUP_H_

/**
 * This function contains macros and function for automatic cleanup with clean_on_drop().
 */
#include <stdint.h>
#include <stdlib.h>

#ifndef ASCLIB_SYNC_H_
typedef struct _ASCLIB_Mpsc_rx_Handle      *mpsc_rx_t;
typedef struct _ASCLIB_Mpsc_tx_Handle      *mpsc_tx_t;

typedef struct _ASCLIB_Oneshot_RX_     *oneshot_rx_t;
typedef struct _ASCLIB_Oneshot_TX_     *oneshot_tx_t;

void mpsc_drop_tx(mpsc_tx_t pair);
void mpsc_drop_rx(mpsc_rx_t pair);

void oneshot_drop_rx(oneshot_rx_t tx);
void oneshot_drop_tx(oneshot_tx_t tx);
#endif

#ifndef ASCLIB_NET_TCP_H_
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
#endif
#ifndef ASCLIB_NET_UDP_H_
typedef struct _ASCLIB_UDP_Peer_ {
    uint8_t ip_addr[4];
    uint16_t port;
} udp_peer_t;

typedef struct _ASCLIB_UDP_Node_ {
    int fd;
    udp_peer_t bind_addr;
    int is_connected;       
    udp_peer_t target;
} udp_node_t;
#endif

#include <unistd.h>
#include <sys/socket.h>
#include <stdio.h>

typedef struct {
    void(*drop_)(void*);
    void* obj;
} object_drop_autocleanup_t;



// Unsafe versions of drop functions without ownership verification
void __mpsc_forcedrop_rx(mpsc_rx_t rx);
void __oneshot_forcedrop_rx(oneshot_rx_t);
void __oneshot_forcedrop_tx(oneshot_tx_t tx);
void __release_fd(int fd);

// Deallocators
static inline void __drop_pointer(void* p) {if (p)  free(p);}
static inline void __drop_tcplistener(void* p) {
    if (!p) return;
    tcp_listener_t* l = (tcp_listener_t*)p;
    if (l->fd >= 0) shutdown(l->fd, SHUT_RDWR);
    free(l);
}
static inline void __drop_tcpclient(void* p) {
    if (!p) return;
    tcp_client_t* l = (tcp_client_t*)p;
    if (l->fd >= 0) shutdown(l->fd, SHUT_RDWR);
    free(l);
}

static inline void __drop_udpnode(void* p) {
    if (!p) return;
    udp_node_t* u = (udp_node_t*)p;
    if (u->fd >= 0) shutdown(u->fd, SHUT_RDWR);
    free(u);
}

static inline void __drop_fd(void* fd) { if ((int)(uintptr_t)fd >= 0) { close((int)(uintptr_t)fd); }}
static inline void __drop_oneshot_tx(oneshot_tx_t tx) { if (tx) __oneshot_forcedrop_tx(tx); }
static inline void __drop_oneshot_rx(oneshot_rx_t rx) { if (rx) __oneshot_forcedrop_rx(rx); }
static inline void __drop_mpsc_tx(mpsc_tx_t tx) { if (tx) mpsc_drop_tx(tx); }
static inline void __drop_mpsc_rx(mpsc_rx_t rx) { if (rx) __mpsc_forcedrop_rx(rx); }

// Function for resgistering each cleanup 'dutty'
void __drop_cleanup_register(object_drop_autocleanup_t oda);


#define drop_func_of(x) _Generic((x),             \
    int: (void(*)(void*))__drop_fd,               \
    oneshot_tx_t: (void(*)(void*))__drop_oneshot_tx, \
    oneshot_rx_t: (void(*)(void*))__drop_oneshot_rx, \
    struct _ASCLIB_TCP_Listener_*: __drop_tcplistener, \
    struct _ASCLIB_TCP_Client_*: __drop_tcpclient,     \
    struct _ASCLIB_UDP_Node_*: __drop_udpnode,           \
    mpsc_tx_t: (void(*)(void*))__drop_mpsc_tx,       \
    mpsc_rx_t: (void(*)(void*))__drop_mpsc_rx,       \
    default: (void(*)(void*))__drop_pointer          \
)


#define clean_on_drop(var_expr) ({                                      \
    __auto_type _tmp_val = (var_expr);                                  \
    object_drop_autocleanup_t _oda = {                               \
        .drop_ = drop_func_of(_tmp_val),                             \
        .obj = (void*)(uintptr_t)_tmp_val                            \
    };                                                               \
    __drop_cleanup_register(_oda);                                   \
    _tmp_val;                                                            \
})


#define drop_task(handle) __task_drop_impl(handle)
#define drop __task_drop_impl((task_handle_t){ -1 } )

#endif // DEMO_ASCLIB_CLEANUP_H_

