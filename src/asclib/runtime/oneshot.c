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

#define ASSERT_OWNER(chan, tx_rx, fn) do {\
    task_handle_t ha = (tx_rx)? chan->rx_owner : chan->tx_owner;\
    if (ha == (size_t)-1) break;\
    if (ha != GET_FULL_HANDLE()) {\
        asc_chan_error(CHAN_PANIC_UNAUTHORIZED, fn, \
            (any_t)ha, (any_t)GET_FULL_HANDLE(), (any_t)(void*)"oneshot");\
    }\
} while(0);

#define ASSERT_FLIGHT(chan, tx_rx, fn) do {\
    task_handle_t ha = (tx_rx)? chan->rx_owner : chan->tx_owner;\
    chan_err_t ch = (tx_rx)? CHAN_ERROR_RX_LOCKED : CHAN_ERROR_TX_LOCKED;\
    if (ha != (size_t)-2) {\
        asc_chan_error(ch, fn, \
            (any_t)ha, (any_t)GET_FULL_HANDLE(), (any_t)(void*)"oneshot");\
    }\
} while(0);



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
    task_handle_t caller;
};

struct _ORECV_arg {
    oneshot_t* rx;
    void* buffer;
    task_handle_t caller;
};

typedef enum {
    ST_CHAN_READY,    // Channel is open and empty
    ST_CHAN_CLOSED,   // The other end was dropped/closed
    ST_CHAN_USED,     // Payload was already sent
} chan_status_t;


static chan_err_t check_some_errors(oneshot_t* ch, void* ptr, int tx_rx, task_handle_t caller);
static void notify_waiters(oneshot_t* chan);


static void* __internal_oneshot_recv(void* __arg) {
    struct _ORECV_arg* args = (struct _ORECV_arg*)__arg;
    void* buffer = args->buffer; (void)buffer;
    oneshot_t* chan = args->rx; (void)chan;
    task_handle_t caller = args->caller;

    free(__arg);


    chan_err_t cerr = check_some_errors(chan, buffer, 1, caller);
    if (cerr != CHAN_OK) task_return(chan_err_t, cerr);

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
    notify_waiters(chan);


    task_return(chan_err_t, CHAN_OK);
}

static void* __internal_oneshot_send(void* __arg) {
    struct _OSEND_arg* args = (struct _OSEND_arg*)__arg;
    void* payload = args->payload; (void)payload;
    oneshot_t* chan = args->tx;     (void)chan;
    task_handle_t caller = args->caller;

    free(__arg);

    chan_err_t cerr = check_some_errors(chan, payload, 0, caller);
    if (cerr != CHAN_OK) task_return(chan_err_t, cerr);

    memcpy(chan->payload, payload, chan->payload_sz);
    chan->sent = 1;

    worker_t* w = tl_worker;

    if (chan->rx_waiter != (size_t)-2) {
        w->tasks.arr[LOWER_32(chan->rx_waiter)]->status = READY;
        w->tasks.arr[LOWER_32(chan->rx_waiter)]->w_reason = WAIT_NONE;
    }

    task_return(chan_err_t, CHAN_OK);
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
    chan->tx_alive = 1;
    chan->sent = 0;
    chan->action_waiters = NULL;
    chan->payload_sz = payload_size;
    chan->tx_owner = GET_FULL_HANDLE();
    chan->payload = malloc(sizeof(payload_size));
    chan->magic = (__track_closure)? OS_MAGIC_TRACK : 0;
    return (oneshot_pair_t)chan;
}

static void oneshot_free(oneshot_pair_t* p) {
    if (!(*p)) return;
    oneshot_t** c = (oneshot_t**)p;
    free((*c)->payload);
    (*c)->payload = NULL;
    u64al_dealloc(&((*c)->action_waiters));
    (*c)->action_waiters = NULL;
    free((*c));
    *c = NULL;
}

static chan_err_t check_some_errors(oneshot_t* ch, void* ptr, int tx_rx, task_handle_t caller) {

    char* f = (tx_rx)? "CHANNELS::asc_oneshot_recv()" : "CHANNELS::asc_oneshot_send()";

    
    if (!ch || !ptr){ asc_chan_error(CHAN_ERROR_NULL_PTR, 
        f, (any_t)0, (any_t)0, (any_t)0); return CHAN_ERROR_NULL_PTR;}

    if (!tx_rx) {
        if (ch->tx_owner != caller) {
            asc_chan_error(CHAN_PANIC_UNAUTHORIZED, f, 
                (any_t)ch->rx_owner, (any_t)caller, (any_t)(void*)"oneshot");
        }
        if (ch->sent == 1) {
            asc_chan_error(CHAN_ERROR_ALREADY_USED, f, (any_t)0, (any_t)0, (any_t)0);
            return CHAN_ERROR_ALREADY_USED;
        } 
    } else {
        if (ch->rx_owner != caller) {
            asc_chan_error(CHAN_PANIC_UNAUTHORIZED, f, 
                (any_t)ch->rx_owner, (any_t)caller, (any_t)0);
        }
        if (ch->rx_waiter == (size_t)-1) {
            asc_chan_error(CHAN_ERROR_ALREADY_USED, f, (any_t)0, (any_t)0, (any_t)0);
            return CHAN_ERROR_ALREADY_USED;
        }
    }

    if (ch->sent == 0 && ch->tx_alive == 0) {
        asc_chan_error(CHAN_ERROR_DEAD_CHANNEL, f, (any_t)0, (any_t)0, (any_t)0);
        return CHAN_ERROR_DEAD_CHANNEL;
    }

    return 0;
}

chan_status_t oneshot_status(void* any_end) {
    if (!any_end) return ST_CHAN_CLOSED;

    oneshot_t* chan = any_end;
    if (chan->rx_owner == (size_t)-1 || chan->tx_owner == (size_t)-1) 
        return ST_CHAN_CLOSED;

    if (chan->sent == 1 || chan->tx_alive == 0)
        return ST_CHAN_USED;

    return ST_CHAN_READY;
}


oneshot_rx_ref_t oneshot_move_rx(oneshot_pair_t rx) {
    oneshot_t* chan = (oneshot_t*)rx;
    worker_t* w = tl_worker;
    ASSERT_CONTEXT(w, "CHANNELS::oneshot_move_rx()");
    if (chan->rx_owner == (size_t)-2) return (oneshot_rx_ref_t)chan;
    ASSERT_OWNER(chan, 1, "CHANNELS::oneshot_move_rx()");
    chan->rx_owner = -2;
    return(oneshot_rx_ref_t)chan;
}

oneshot_rx_t oneshot_take_rx(oneshot_rx_ref_t ref) {
    oneshot_t* chan = (oneshot_t*)ref;
    worker_t* w = tl_worker;
    ASSERT_CONTEXT(w, "CHANNELS::oneshot_move_rx()");
    ASSERT_FLIGHT(chan, 1, "CHANNELS::oneshot_move_rx()");
    chan->rx_owner = GET_FULL_HANDLE();
    return(oneshot_rx_t)chan;
}

oneshot_tx_ref_t oneshot_move_tx(oneshot_pair_t tx) {
    oneshot_t* chan = (oneshot_t*)tx;
    worker_t* w = tl_worker;
    ASSERT_CONTEXT(w, "CHANNELS::oneshot_move_tx()");
    if (chan->tx_owner == (size_t)-2) return (oneshot_tx_ref_t)chan;
    ASSERT_OWNER(chan, 0, "CHANNELS::oneshot_move_tx()");
    chan->tx_owner = -2;
    return(oneshot_tx_ref_t)chan;
}

oneshot_tx_t oneshot_take_tx(oneshot_tx_ref_t ref) {
    oneshot_t* chan = (oneshot_t*)ref;
    worker_t* w = tl_worker;
    ASSERT_CONTEXT(w, "CHANNELS::oneshot_move_rx()");
    ASSERT_FLIGHT(chan, 0, "CHANNELS::oneshot_move_rx()");
    chan->tx_owner = GET_FULL_HANDLE();
    return(oneshot_tx_t)chan;
}

void __oneshot_forcedrop_tx(oneshot_tx_t tx) {
    oneshot_t* chan = (oneshot_t*)tx;
    worker_t* w = tl_worker;
    ASSERT_CONTEXT(w, "CHANNELS::oneshot_drop_tx()");


    if (chan->rx_waiter != (size_t)-1 && chan->rx_waiter != (size_t)-2 && !IS_STALE(chan->rx_waiter)) {
        w->tasks.arr[LOWER_32(chan->rx_waiter)]->status = READY;
        w->tasks.arr[LOWER_32(chan->rx_waiter)]->w_reason = WAIT_NONE;
        chan->rx_waiter = -1;
    }

    notify_waiters(chan);

    chan->tx_alive = 0;
    if (chan->rx_owner == (size_t)-1) {
        switch (chan->magic)
        {
        case OS_MAGIC_TRACK:
            chan->magic = OS_MAGIC_CLOSED;
            break;
        case OS_MAGIC_CLOSED:
            asc_chan_error(CHAN_WARN_DOUBLE_DROP, 
                "CHANNELS::oneshot_drop_tx()", (any_t)0, (any_t)0, (any_t)0);
            break;
        
        default:
            oneshot_free((oneshot_pair_t*)&chan);
            break;
        }
    }
}

void oneshot_drop_tx(oneshot_tx_t tx) {
    oneshot_t* chan = (oneshot_t*)tx;
    worker_t* w = tl_worker;
    ASSERT_CONTEXT(w, "CHANNELS::oneshot_drop_tx()");
    ASSERT_OWNER(chan, 0, "CHANNELS::oneshot_drop_tx()");
    __oneshot_forcedrop_tx(tx);
}

void __oneshot_forcedrop_rx(oneshot_rx_t rx) {
    oneshot_t* chan = (oneshot_t*)rx;
    worker_t* w = tl_worker;
    ASSERT_CONTEXT(w, "CHANNELS::oneshot_drop_rx()");

    chan->rx_owner = (size_t)-1;
    if (chan->tx_alive == 0) {
        switch (chan->magic)
        {
        case OS_MAGIC_TRACK:
            chan->magic = OS_MAGIC_CLOSED;
            break;
        case OS_MAGIC_CLOSED:
            asc_chan_error(CHAN_WARN_DOUBLE_DROP, 
                "CHANNELS::oneshot_drop_rx()", (any_t)0, (any_t)0, (any_t)0);
            break;
        
        default:
            oneshot_free((oneshot_pair_t*)&chan);
            break;
        }
    }

    notify_waiters(chan);
}

void oneshot_drop_rx(oneshot_rx_t rx) {
    oneshot_t* chan = (oneshot_t*)rx;
    worker_t* w = tl_worker;
    ASSERT_CONTEXT(w, "CHANNELS::oneshot_drop_rx()");
    ASSERT_OWNER(chan, 1, "CHANNELS::oneshot_drop_rx()");
    __oneshot_forcedrop_rx(rx);
}

void oneshot_assert_closed(oneshot_pair_t pair) {
    if (!pair) { asc_chan_error(CHAN_ERROR_NULL_PTR, "CHANNELS::oneshot_assert_closed()", (any_t)0, (any_t)0, (any_t)0)  ;return;}

    oneshot_t* chan = (oneshot_t*)pair;

    switch (chan->magic)
    {
    case OS_MAGIC_CLOSED:
        oneshot_free((oneshot_pair_t*)&chan);
        break;

    case OS_MAGIC_TRACK:
        if (chan->tx_alive == 1) {
            asc_error(PANIC_GENERIC_ASSERTION, "CHANNELS::oneshot_assert_closed()", 
                (any_t)(void*)"\033[1m\033[97mTX channel end was not closed\033[0m.");
        } else if (chan->rx_owner != (size_t)-1) {
            asc_error(PANIC_GENERIC_ASSERTION, "CHANNELS::mpsc_assert_closed()", 
                (any_t)(void*)"\033[1m\033[97mRX channel end was not closed\033[0m.");
        } else {
            oneshot_free((oneshot_pair_t*)&chan);
        }
        break;
    
    default:
        asc_error(WARN_TRACKING_UNMARKED_OBJECT, "CHANNELS::mpsc_assert_closed()", (any_t)(void*)"oneshot channel");
        break;
    }
}

task_handle_t asc_oneshot_recv(oneshot_rx_t rx, void* buffer) {

    worker_t* w = tl_worker;    
    ASSERT_CONTEXT(w, "CHANNELS::asc_oneshot_recv()");


    struct _ORECV_arg* a = malloc(sizeof(struct _ORECV_arg));
    a->buffer = buffer;
    a->rx = (oneshot_t*)rx;
    a->caller = GET_FULL_HANDLE();
    
    return __task_create_ex( 
        (task_entry_t){.fptr = __internal_oneshot_recv, .arg_malloced = 0, .value = (void*)a, .arg_size = sizeof(void*)},
        4096,
        1
    );
}

task_handle_t asc_oneshot_send(oneshot_tx_t tx, void* payload) {

    worker_t* w = tl_worker;    
    ASSERT_CONTEXT(w, "CHANNELS::asc_oneshot_send()");

    struct _OSEND_arg* a = malloc(sizeof(struct _OSEND_arg));
    a->payload = payload;
    a->tx = (oneshot_t*)tx;
    a->caller = GET_FULL_HANDLE();
    
    return __task_create_ex( 
        (task_entry_t){.fptr = __internal_oneshot_send, .arg_malloced = 0, .value = (void*)a, .arg_size = sizeof(void*)},
        4096,
        1
    );
}

static void notify_waiters(oneshot_t* chan) {
    if (!chan || !chan->action_waiters) return;
    worker_t* w = tl_worker;
    

    while(chan->action_waiters->size > 0) {
        task_handle_t th = u64al_pop(chan->action_waiters, 0);
        TCB* t = w->tasks.arr[LOWER_32(th)];
        if (TCB_AT(LOWER_32(th)) && !IS_STALE(th) && t->status == WAITING && t->w_reason == WAIT_CHANNEL) {
            t->status = READY;
        }
    }
}


async(__internal_wait_change, void* any_end) {

    oneshot_t* chan = (oneshot_t*)arg(any_end);
    if (!chan) {
        asc_chan_error(CHAN_ERROR_NULL_PTR, "CHANNELS::oneshot_wait_change()", (any_t)0, (any_t)0, (any_t)0); 
        task_return_void(); 
    }

    worker_t* w = tl_worker;
    TCB* t = w->tasks.arr[w->tasks.current];

    if (!chan->action_waiters) {
        chan->action_waiters = u64al_alloc(2);
    }

    t->status = WAITING;
    t->w_reason = WAIT_CHANNEL;
    u64al_append(chan->action_waiters, (uint64_t)GET_FULL_HANDLE());
    yield;

    end_return_void();
}

task_handle_t oneshot_wait_change(void* any_end) {
    worker_t* w = tl_worker;
    ASSERT_CONTEXT(w, "CHANNELS::oneshot_wait_change()");

    return task_spawn_ex(__internal_wait_change, 4096, 1, .any_end = any_end);
}



