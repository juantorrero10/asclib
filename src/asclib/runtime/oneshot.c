#include <string.h>
#include <stdio.h>


#include <channels.h>
#include <task_control.h>
#include <task_mem.h>
#include <dispatcher.h>
#include <ascerr.h>
#include <user_lib.h>

#define __DEBUG
#include <log.h>

#define OS_MAGIC_TRACK 0x4F4E5348
#define OS_MAGIC_CLOSED 0xDEADBEEF

// Managed by task_mem.c
extern TLS g_arrlist_t* generation_map;

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


static void* __internal_oneshot_recv(void* __arg) {
    struct _ORECV_arg* args = (struct _ORECV_arg*)__arg;
    void* buffer = args->buffer; (void)buffer;
    oneshot_t* chan = args->rx; (void)chan;

    free(__arg);

    worker_t* w = tl_worker;

    if (chan->sent == 0) {
        //Wait
        STATUS_OF(w->tasks.current) = WAITING;
        w->tasks.arr[w->tasks.current]->w_reason = WAIT_CHANNEL;
        chan->rx_waiter = GET_FULL_HANDLE();
        __task_yield_impl();
    }

    memcpy(buffer, chan->payload, chan->payload_sz);
    chan->rx_waiter = -1;


    task_return(asclib_err_channels_t, CHAN_OK);
}

static void* __internal_oneshot_send(void* __arg) {
    struct _OSEND_arg* args = (struct _OSEND_arg*)__arg;
    void* payload = args->payload; (void)payload;
    oneshot_t* chan = args->tx;     (void)chan;

    free(__arg);

    memcpy(chan->payload, payload, chan->payload_sz);
    chan->sent = 1;
    chan->tx_alive = 0;

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
    chan->rx_owner = GET_FULL_HANDLE();
    chan->rx_waiter = -2;
    chan->tx_alive = 0;
    chan->payload_sz = payload_size;
    chan->tx_owner = GET_FULL_HANDLE();
    chan->payload = malloc(sizeof(payload_size));
    chan->magic = (__track_closure)? OS_MAGIC_TRACK : 0;
    return (oneshot_pair_t)chan;
}

void oneshot_free(oneshot_pair_t* p) {
    if (!(*p)) return;
    oneshot_t** c = (oneshot_t**)p;
    free((*c)->payload);
    free((*c));
    *c = NULL;
}

static task_handle_t check_some_errors(oneshot_t* ch, void* ptr, int tx_rx) {

    char* f = (tx_rx)? "CHANNELS::asc_oneshot_send()" : "CHANNELS::asc_oneshot_recv()";

    worker_t* w = tl_worker;
    ASSERT_CONTEXT(w, f);

    task_handle_t te = GET_FULL_HANDLE();

    
    if (!ch || !ptr){ asc_chan_error(CHAN_ERROR_NULL_PTR, 
        f, (any_t)0, (any_t)0, (any_t)0); return -1;}

    if (!tx_rx) {
        if (ch->tx_owner != te) {
            asc_chan_error(CHAN_PANIC_UNAUTHORIZED, f, 
                (any_t)ch->rx_owner, (any_t)te, (any_t)0);
        }
        if (ch->sent == 1) {
            asc_chan_error(CHAN_ERROR_ALREADY_USED, f, (any_t)0, (any_t)0, (any_t)0);
            return -1;
        } 
    } else {
        if (ch->rx_owner != te) {
            asc_chan_error(CHAN_PANIC_UNAUTHORIZED, f, 
                (any_t)ch->rx_owner, (any_t)te, (any_t)0);
        }
        if (ch->rx_waiter == (size_t)-1) {
            asc_chan_error(CHAN_ERROR_ALREADY_USED, f, (any_t)0, (any_t)0, (any_t)0);
            return -1;
        }
    }

    if (ch->sent == 0 && ch->tx_alive == 0) {
        asc_chan_error(CHAN_ERROR_DEAD_CHANNEL, f, (any_t)0, (any_t)0, (any_t)0);
        return -1;
    }

    return 0;
}

task_handle_t asc_oneshot_recv(oneshot_rx_t rx, void* buffer) {

    if (check_some_errors((oneshot_t*)rx, buffer, 0) != 0) return -1;


    struct _ORECV_arg* a = malloc(sizeof(struct _ORECV_arg));
    a->buffer = buffer;
    a->rx = (oneshot_t*)rx;
    
    return __task_create_ex( 
        (task_entry_t){.fptr = __internal_oneshot_recv, .arg_malloced = 0, .value = (void*)a, .arg_size = sizeof(void*)},
        2048,
        1
    );
}

task_handle_t asc_oneshot_send(oneshot_tx_t tx, void* payload) {

    if (check_some_errors((oneshot_t*)tx, payload, 1) != 0) return -1;

    struct _OSEND_arg* a = malloc(sizeof(struct _OSEND_arg));
    a->payload = payload;
    a->tx = (oneshot_t*)tx;
    
    return __task_create_ex( 
        (task_entry_t){.fptr = __internal_oneshot_send, .arg_malloced = 0, .value = (void*)a, .arg_size = sizeof(void*)},
        2048,
        1
    );
}



