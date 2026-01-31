#ifndef CHANNELS_H_
#define CHANNELS_H_

#include <collections.h>

typedef struct _ASCLIB_Oneshot_Channel {
    uint32_t magic;
    void* payload;
    int sent;
    task_handle_t tx_owner;
    task_handle_t rx_owner;
} oneshot_t;

typedef struct _ASCLIB_mpsc_Channel {
    uint32_t magic;

    void* buffer;
    size_t packet_size;
    size_t capacity;
    size_t count;
    size_t head, tail;

    // Single consumer -> 1 receiving waiter
    task_handle_t rx_waiter; // The waiter is the spawned task that blocks itself until data is send. 
    task_handle_t rx_owner;  // The owner is the task which is allowed to call recv and move the rx
    // Multiple produces
    u64_arrlist_t* tx_waiters;
    size_t tx_rc;            // Reference count. Calling drop subtracts 1 and frees when reaching 0. 
}mpsc_t;

#endif // CHANNELS_H_