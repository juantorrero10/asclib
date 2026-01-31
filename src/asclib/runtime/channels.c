#include <assert.h>
#include <string.h>

#include <collections.h>
#include <task_mem.h>
#include <task_control.h>
#include <ascerr.h>
#include <user_lib.h>
#include <dispatcher.h>

#define __DEBUG

#include <stdio.h>
#include <log.h>

#define ENABLE_GUARD_PAGE 1
#define MPSC_PROD_INITAL 4
#define MPSC_RX_STATE_REF (task_handle_t)-2
#define MPSC_RX_STATE_CLOSED (task_handle_t)-1

#define MPSC_MAGIC_TRAKIING 0x4D505343
#define MPSC_MAGIC_CLOSED 0xDEADBEEF

#define ASSERT_STATUS(status_src, expected, task_id, function) do{\
    if (status_src != expected) {\
    asc_chan_error(CHAN_PANIC_STATE_CORRUPTION, function, (any_t)task_id, (any_t)expected, (any_t)status_src);}\
} while (0)

#define ASSERT_OWNER(req, owner, fn) do {\
    if (req != owner) {\
        asc_chan_error(CHAN_PANIC_UNAUTHORIZED, fn, (any_t)req, (any_t)owner, (any_t)0);\
    }\
}while (0)

#define ASSERT_NOT_NULL(ptr, fn) do{\
    if (!ptr){\
        asc_chan_error(CHAN_ERROR_NULL_PTR, fn, (any_t)0, (any_t)0, (any_t)0);\
        task_return(asclib_err_channels_t, CHAN_ERROR_NULL_PTR);\
    }\
} while(0)



// Opaque types for users
typedef struct _ASCLIB_Mpsc_rx_Handle      *mpsc_rx_t;
typedef struct _ASCLIB_Mpsc_rx_ref_Handle  *mpsc_rx_ref_t;
typedef struct _ASCLIB_Mpsc_tx_Handle      *mpsc_tx_t;
typedef struct _ASCLIB_Mpsc_pair_Handle    *mpsc_pair_t;


struct _IMS_args {
    mpsc_t* tx;
    void* data;
};

struct _IMR_args {
    mpsc_t* rx;
    void* buffer;
};

static void* __internal_mpsc_recv(void* __arg) {
    struct _IMR_args* args = (struct _IMR_args*)__arg;
    mpsc_t* chan = args->rx;
    void* buffer = (*(struct _IMR_args*)__arg).buffer;

    free(__arg);

    worker_t* w = tl_worker;
    ASSERT_CONTEXT(w, "CHANNELS::mspc_recv()");
    if (chan->rx_owner == MPSC_RX_STATE_CLOSED || chan->rx_owner == MPSC_RX_STATE_REF) {
        asc_chan_error(CHAN_ERROR_RX_INVALID, "CHANNELS::mpsc_recv()", (any_t)0, (any_t)0, (any_t)0);
        task_return(asclib_err_channels_t, CHAN_ERROR_RX_INVALID);
    }
    ASSERT_NOT_NULL(buffer, "CHANNELS::mpsc_send()");
    size_t curr = w->tasks.current;
    TCB* t = w->tasks.arr[curr];

    // If channel is empty, block task
    if (chan->count == 0) {
        t->status = WAITING;
        t->w_reason = WAIT_CHANNEL;
        chan->rx_waiter = curr;
        __task_yield_impl();
    }


    // Retrieve the data
    memcpy(buffer, (uint8_t*)chan->buffer + chan->packet_size * chan->head, chan->packet_size);
    chan->head = (chan->head + 1) % chan->capacity;
    chan->count--;

    // Remove one waiter
    if (chan->tx_waiters->size > 0) {
        t = w->tasks.arr[u64al_pop_front(chan->tx_waiters)];
        ASSERT_STATUS(t->status, WAITING, u64al_pop_front(chan->tx_waiters), "CHANNELS::mspc_recv()");
        t->status = READY;
        t->w_reason = WAIT_NONE;
    }


    task_return(asclib_err_channels_t, CHAN_OK);
}

// Intenal
static void* __internal_mpsc_send(void* __arg) {
    mpsc_t* chan = ((struct _IMS_args*)__arg)->tx;
    void* data = ((struct _IMS_args*)__arg)->data;

    free(__arg);

    worker_t* w = tl_worker;
    ASSERT_CONTEXT(w, "CHANNELS::mspc_send()");
    if (chan->rx_owner == MPSC_RX_STATE_CLOSED) {
        asc_chan_error(CHAN_ERROR_RX_INVALID, "CHANNELS::mpsc_send()", (any_t)0, (any_t)0, (any_t)0);
        task_return(asclib_err_channels_t, CHAN_ERROR_RX_INVALID);
    }
    if (chan->magic == MPSC_MAGIC_CLOSED) {
        asc_chan_error(CHAN_ERROR_DEAD_CHANNEL, "CHANNELS::mpsc_send()", (any_t)0, (any_t)0, (any_t)0);
        task_return(asclib_err_channels_t, CHAN_ERROR_DEAD_CHANNEL);
    }
    ASSERT_NOT_NULL(data, "CHANNELS::mpsc_send()");
    size_t curr = w->tasks.current;
    TCB* t = w->tasks.arr[curr];

    // Channel is full, wait
    if (chan->count >= chan->capacity) {
        u64al_append(chan->tx_waiters, curr);
        t->status = WAITING;
        t->w_reason = WAIT_CHANNEL;
        __task_yield_impl();
    }

    //Send data.
    void* dst = (uint8_t*)chan->buffer + (chan->tail * chan->packet_size);
    memcpy(dst, data, chan->packet_size);

    // Increase the tail
    chan->tail = (chan->tail + 1) % chan->capacity;
    chan->count++;

    // Wake up consumer
    if (chan->rx_waiter != (task_handle_t)-1) {
        t = w->tasks.arr[chan->rx_waiter];
        ASSERT_STATUS(t->status, WAITING, chan->rx_waiter, "CHANNELS::mspc_send()");
        t->status = READY;
        t->w_reason = WAIT_NONE;
        chan->rx_waiter = (task_handle_t)-1;
    }

    task_return(asclib_err_channels_t, CHAN_OK);
}

mpsc_pair_t mpsc_new(size_t packet_size, size_t max_messages, int __track_closure) {
    worker_t* w = tl_worker;
    ASSERT_CONTEXT(w, "CHANNELS::mpsc_new()");
    if (!(packet_size > 0) || !(max_messages > 0)) {
        asc_chan_error(CHAN_WARN_RETURN_NULL, "CHANNELS::mpsc_new()", (any_t)0, (any_t)0, (any_t)0);
        return NULL;
    }
    mpsc_t* chan = malloc(sizeof(mpsc_t));
    if (!chan) { asc_chan_error(CHAN_PANIC_STATE_CORRUPTION, "mpsc_new", (any_t)(void*)"Couldn't allocate object", (any_t)0, (any_t)0); }
    chan->buffer = malloc(packet_size * max_messages);
    chan->capacity = max_messages;
    chan->packet_size = packet_size;
    chan->magic = (__track_closure)? MPSC_MAGIC_TRAKIING : 0x00000000;
    chan->count = 0;
    chan->head = 0;
    chan->tail = 0;
    chan->rx_waiter = (task_handle_t)-1;
    chan->tx_waiters = u64al_alloc(MPSC_PROD_INITAL);
    chan->tx_rc = 0;
    chan->rx_owner = w->tasks.current;
    return (mpsc_pair_t)chan;
}



mpsc_rx_ref_t mpsc_move_rx(mpsc_pair_t pair) {
    mpsc_t* chan = (mpsc_t*)pair;

    worker_t* w = tl_worker;
    ASSERT_CONTEXT(w, "CHANNELS::mpsc_move_rx()");

    ASSERT_OWNER(w->tasks.current, chan->rx_owner, "CHANNELS::mpsc_move_rx()");

    chan->rx_owner = MPSC_RX_STATE_REF;

    return (mpsc_rx_ref_t)pair;
}

mpsc_rx_t mpsc_take_rx(mpsc_rx_ref_t rx_reference) {
    mpsc_t* chan = (mpsc_t*)rx_reference;
    worker_t* w = tl_worker;
    ASSERT_CONTEXT(w, "CHANNELS::mpsc_take_rx()");
    if(chan->rx_owner != MPSC_RX_STATE_REF) 
    {
        asc_chan_error(CHAN_ERROR_RX_LOCKED, "CHANNELS::mpsc_take_rx()", (any_t)w->tasks.current, (any_t)0, (any_t)0);
        return NULL;
    }


    chan->rx_owner = w->tasks.current;

    return (mpsc_rx_t)rx_reference;
}

mpsc_rx_ref_t mpsc_release_rx(mpsc_rx_t rx) {
    mpsc_t* chan = (mpsc_t*)rx;
    worker_t* w = tl_worker;
    ASSERT_CONTEXT(w, "CHANNELS::mpsc_release_rx()");
    ASSERT_OWNER(w->tasks.current, chan->rx_owner, "CHANNELS::mpsc_release_rx()");

    chan->rx_owner = MPSC_RX_STATE_REF;

    return (mpsc_rx_ref_t)rx;
}

mpsc_tx_t mpsc_clone_tx(mpsc_pair_t pair) {
    mpsc_t* chan = (mpsc_t*)pair;
    chan->tx_rc++;
    return (mpsc_tx_t)chan;
}

void __mpsc_free(mpsc_t** chan) {
    free((*chan)->buffer);
    u64al_dealloc(&(*chan)->tx_waiters);
    free((*chan));
    *chan = NULL;
}

void mpsc_drop_tx(mpsc_tx_t tx) {
    mpsc_t* chan = (mpsc_t*)tx;
    chan->tx_rc--;

    if (chan->tx_rc == 0 && chan->rx_owner == MPSC_RX_STATE_CLOSED) {
        switch (chan->magic)
        {
        case MPSC_MAGIC_TRAKIING:
            chan->magic = MPSC_MAGIC_CLOSED;
            break;

        case MPSC_MAGIC_CLOSED:
            asc_chan_error(CHAN_WARN_DOUBLE_DROP, "CHANNELS::mpsc_drop_tx()", (any_t)0, (any_t)0, (any_t)0);
            break;
        
        default:
            __mpsc_free(&chan);
            break;
        }
    }
}

void mpsc_drop_rx(mpsc_rx_t rx) {
    mpsc_t* chan = (mpsc_t*)rx;
    chan->rx_owner = MPSC_RX_STATE_CLOSED;

    if (chan->tx_rc == 0) {
        switch (chan->magic)
        {
        case MPSC_MAGIC_TRAKIING:
            chan->magic = MPSC_MAGIC_CLOSED;
            break;

        case MPSC_MAGIC_CLOSED:
            asc_chan_error(CHAN_WARN_DOUBLE_DROP, "CHANNELS::mpsc_drop_rx()", (any_t)0, (any_t)0, (any_t)0);
            break;
        
        default:
            __mpsc_free(&chan);
            break; 
        }
    }
}

void mpsc_assert_closed(mpsc_pair_t pair) {
    if (!pair) { asc_chan_error(CHAN_ERROR_NULL_PTR, "CHANNELS::mpsc_assert_closed()", (any_t)0, (any_t)0, (any_t)0)  ;return;}

    mpsc_t* chan = (mpsc_t*)pair;

    switch (chan->magic)
    {
    case MPSC_MAGIC_CLOSED:
        __mpsc_free(&chan);
        break;

    case MPSC_MAGIC_TRAKIING:
        if (chan->tx_rc > 0) {
            char buff[200];
            sprintf(buff, "\033[1m\033[97mtx reference count = %zu ( > 0 )\033[0m, \033[4mone or multiple producers did not \033[1m\033[97mdrop\033[0m\033[4m its reference\033[0m", chan->tx_rc);
            asc_error(PANIC_GENERIC_ASSERTION, "CHANNELS::mpsc_assert_closed()", 
                (any_t)(void*)buff);
        } else if (chan->rx_owner != MPSC_RX_STATE_CLOSED) {
            asc_error(PANIC_GENERIC_ASSERTION, "CHANNELS::mpsc_assert_closed()", 
                (any_t)(void*)"\033[4mThe consumer \033[1m\033[97mdid not drop\033[0m\033[4m its rx reference\033[0m");
        } else {
            __mpsc_free(&chan);
        }
        break;
    
    default:
        asc_error(WARN_TRACKING_UNMARKED_OBJECT, "CHANNELS::mpsc_assert_closed()", (any_t)(void*)"mpsc channel");
        break;
    }
}



task_handle_t asc_mpsc_send(mpsc_tx_t tx, void* data) {

    struct _IMS_args* a = malloc(sizeof(struct _IMS_args));
    *a = (struct _IMS_args){ .data = data, .tx = (mpsc_t*)tx };

    task_entry_t te = {
        .fptr = __internal_mpsc_send,
        .arg_malloced = 0,
        .value = a,
        .n_pages = 0,
        .arg_size = sizeof(void*)
    };

    return __task_create_ex(te, 4096, ENABLE_GUARD_PAGE);
}

task_handle_t asc_mpsc_recv(mpsc_rx_t rx, void* buff) {
    worker_t* w = tl_worker;
    ASSERT_CONTEXT(w, "CHANNELS::asc_mpsc_recv()");
    ASSERT_OWNER(w->tasks.current, ((mpsc_t*)rx)->rx_owner, "CHANNELS::asc_mpsc_recv()");

    struct _IMR_args* a = malloc(sizeof(struct _IMR_args));
    *a = (struct _IMR_args){ .buffer = buff, .rx = (mpsc_t*)rx };
    //INFO("STRUCT: %p", &a);
    task_entry_t te = {
        .fptr = __internal_mpsc_recv,
        .arg_malloced = 0,
        .value = a,
        .n_pages = 0,
        .arg_size = sizeof(void*)
    };

    return __task_create_ex(te, 4096, ENABLE_GUARD_PAGE);
}