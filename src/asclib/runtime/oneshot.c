#include <channels.h>
#include <task_control.h>
#include <task_mem.h>
#include <dispatcher.h>
#include <ascerr.h>
#include <user_lib.h>

#define OS_MAGIC_TRACK 0x4F4E5348
#define OS_MAGIC_CLOSED 0xDEADBEEF

typedef struct _ASCLIB_Oneshot_pair_   *oneshot_pair_t;
typedef struct _ASCLIB_Oneshot_RX_     *oneshot_rx_t;
typedef struct _ASCLIB_Oneshot_RXREF_  *oneshot_rx_ref_t;
typedef struct _ASCLIB_Oneshot_TX_     *oneshot_tx_t;
typedef struct _ASCLIB_Oneshot_TXREF_  *oneshot_tx_ref_t;

struct _OSEND_arg {
    oneshot_t* tx;
    void* payload;
};

struct _ORECV_arg {
    oneshot_t* rx;
    void* buffer;
};


void* __internal_oneshot_recv(void* __arg) {
    struct _ORECV_arg* args = (struct _ORECV_arg*)__arg;
    void* buffer = args->buffer; (void)buffer;
    oneshot_t* chan = args->rx; (void)chan;

    free(__arg);

    task_return(asclib_err_channels_t, CHAN_OK);
}

void* __internal_oneshot_send(void* __arg) {
    struct _OSEND_arg* args = (struct _OSEND_arg*)__arg;
    void* payload = args->payload; (void)payload;
    oneshot_t* chan = args->tx;     (void)chan;

    free(__arg);

    task_return(asclib_err_channels_t, CHAN_OK);
}

oneshot_pair_t oneshot_new(size_t payload_size, int __track_closure) {
    worker_t* w = tl_worker;
    ASSERT_CONTEXT(w, "CHANNELS::oneshot_new()");
    if (!(payload_size > 0)) {
        asc_chan_error(CHAN_WARN_RETURN_NULL, "CHANNELS::oneshot_new()", (any_t)0, (any_t)0, (any_t)0);
        return NULL;
    }
    oneshot_t* chan = malloc(sizeof(oneshot_t));
    task_handle_t curr = w->tasks.current;
    chan->rx_owner = curr;
    chan->tx_owner = curr;
    chan->payload = malloc(sizeof(payload_size));
    chan->magic = (__track_closure)? OS_MAGIC_TRACK : 0;
    return (oneshot_pair_t)chan;
}

task_handle_t asc_oneshot_recv(oneshot_rx_t rx, void* buffer) {

    struct _ORECV_arg* a = malloc(sizeof(struct _ORECV_arg));
    a->buffer = buffer;
    a->rx = (oneshot_t*)rx;
    
    return __task_create_ex( 
        (task_entry_t){.fptr = NULL, .arg_malloced = 0, .value = (void*)a, .arg_size = sizeof(void*)},
        0,
        0
    );
}

task_handle_t asc_oneshot_send(oneshot_tx_t tx, void* payload) {

    struct _OSEND_arg* a = malloc(sizeof(struct _OSEND_arg));
    a->payload = payload;
    a->tx = (oneshot_t*)tx;
    
    return __task_create_ex( 
        (task_entry_t){.fptr = NULL, .arg_malloced = 0, .value = (void*)a, .arg_size = sizeof(void*)},
        0,
        0
    );
}



