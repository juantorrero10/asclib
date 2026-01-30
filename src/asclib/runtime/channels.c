#include <assert.h>
#include <string.h>

#include <collections.h>
#include <task_mem.h>
#include <task_control.h>
#include <ascerr.h>

#define __DEBUG

#include <stdio.h>
#include <log.h>

#define ENABLE_GUARD_PAGE 1
#define MPSC_PROD_INITAL 4
#define MPSC_RX_STATE_REF (task_handle_t)-2
#define MPSC_RX_STATE_CLOSED (task_handle_t)-1


// Opaque types for users
typedef struct mpsc_t* mpsc_rx_t;
typedef struct mpsc_t* mpsc_rx_ref_t;
typedef struct mpsc_t* mpsc_tx_t;
typedef struct mpsc_t* mpsc_pair_t;


struct _IMS_args {
    mpsc_t* tx;
    void* data;
};

struct _IMR_args {
    mpsc_t* rx;
    void* buffer;
};

void* __internal_mpsc_recv(void* __arg) {
    mpsc_t* chan = (mpsc_t*)((struct _IMR_args*)__arg)->rx;
    void* buffer = ((struct _IMR_args*)__arg)->buffer;
    INFO("CHAN pointer: %p", (void*)chan);
    INFO("CHAN count: %zu", chan->count); // Does it crash HERE?

    INFO("recv");

    worker_t* w = tl_worker;
    ASSERT_CONTEXT(w, "CHANNELS::mspc_recv()");
    assert(chan->rx_owner != MPSC_RX_STATE_CLOSED);
    size_t curr = w->tasks.current;
    TCB* t = w->tasks.arr[curr];

    printf("[DEBUG] Testing write to %p\n", (void*)&t->status);
    
    // Try to write a local variable first
    task_status test_val = WAITING;
    volatile task_status* status_ptr = &t->status;

    INFO("BEFORE");
    
    *status_ptr = test_val; // DOES IT CRASH EXACTLY HERE?
    OKAY("STATUS SET");



    // If channel is empty, block task
    if (chan->count == 0) {
        assert(chan->rx_waiter == (task_handle_t)-1);
        OKAY("HERE");
        t->status = WAITING;
        OKAY("HERE2");
        t->w_reason = WAIT_CHANNEL;
        OKAY("HERE");
        chan->rx_waiter = curr;
        OKAY("HERE");
    }

    OKAY("HERE");

    // Retrieve the data
    memcpy(buffer, (uint8_t*)chan->buffer + chan->packet_size * chan->head, chan->packet_size);
    chan->head = (chan->head + 1) % chan->capacity;
    chan->count--;

    // Remove one waiter
    if (chan->tx_waiters->size > 0) {
        t = w->tasks.arr[u64al_pop_front(chan->tx_waiters)];
        assert(t->status == WAITING);
        t->status = READY;
        t->w_reason = WAIT_NONE;
    }

    return NULL;
}

// Intenal
void* __internal_mpsc_send(void* __arg) {
    mpsc_t* chan = (mpsc_t*)((struct _IMS_args*)__arg)->tx;
    void* data = ((struct _IMS_args*)__arg)->data;

    worker_t* w = tl_worker;
    ASSERT_CONTEXT(w, "CHANNELS::mspc_send()");
    assert(chan->rx_owner != MPSC_RX_STATE_CLOSED);
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
        assert(t->status == WAITING);
        t->status = READY;
        t->w_reason = WAIT_NONE;
        chan->rx_waiter = (task_handle_t)-1;
    }

    return NULL;
}

mpsc_pair_t mpsc_new(size_t packet_size, size_t max_messages) {
    worker_t* w = tl_worker;
    ASSERT_CONTEXT(w, "CHANNELS::mpsc_new()");
    mpsc_t* chan = malloc(sizeof(mpsc_t));
    chan->buffer = malloc(packet_size * max_messages);
    chan->capacity = max_messages;
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

    assert(chan->rx_owner == w->tasks.current);

    chan->rx_owner = MPSC_RX_STATE_REF;

    return (mpsc_rx_ref_t)pair;
}

mpsc_rx_t mpsc_take_rx(mpsc_rx_ref_t rx_reference) {
    mpsc_t* chan = (mpsc_t*)rx_reference;
    worker_t* w = tl_worker;
    ASSERT_CONTEXT(w, "CHANNELS::mpsc_take_rx()");
    assert(chan->rx_owner == MPSC_RX_STATE_REF);

    chan->rx_owner = w->tasks.current;

    return (mpsc_rx_t)rx_reference;
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

    if (chan->tx_rc == 0 && chan->rx_owner == MPSC_RX_STATE_CLOSED) __mpsc_free(&chan);
}

void mpsc_drop_rx(mpsc_rx_t rx) {
    mpsc_t* chan = (mpsc_t*)rx;
    chan->rx_owner = MPSC_RX_STATE_CLOSED;

    if (chan->tx_rc == 0) __mpsc_free(&chan);
}


task_handle_t asc_mpsc_send(mpsc_tx_t tx, void* data) {
    struct _IMS_args a = { (mpsc_t*)tx, data };
    task_entry_t te = {
        .fptr = __internal_mpsc_send,
        .arg_malloced = 0,
        .value = &a,
        .n_pages = 0,
        .arg_size = sizeof(void*)
    };

    return __task_create_ex(te, 1024, ENABLE_GUARD_PAGE);
}

task_handle_t asc_mpsc_recv(mpsc_rx_t rx, void* buff) {
    worker_t* w = tl_worker;
    ASSERT_CONTEXT(w, "CHANNELS::asc_mpsc_recv()");
    assert(((mpsc_t*)rx)->rx_owner == w->tasks.current);

    struct _IMR_args a = { (mpsc_t*)rx, buff };
    task_entry_t te = {
        .fptr = __internal_mpsc_recv,
        .arg_malloced = 0,
        .value = &a,
        .n_pages = 0,
        .arg_size = sizeof(void*)
    };

    return __task_create_ex(te, 4096 * 10, ENABLE_GUARD_PAGE);
}