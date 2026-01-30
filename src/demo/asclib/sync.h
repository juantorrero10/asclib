#ifndef ASCLIB_SYNC_H_
#define ASCLIB_SYNC_H_

#include "core.h"

typedef struct _ASCLIB_One_Shot_Channel oneshot_t;
typedef struct _ASCLIB_spsc_Channel spsc_t;


typedef struct _ASCLIB_mpsc_Channel mpsc_t;
typedef struct mpsc_t* mpsc_rx_t;
typedef struct mpsc_t* mpsc_rx_ref_t;
typedef struct mpsc_t* mpsc_tx_t;
typedef struct mpsc_t* mpsc_pair_t;

// SLEEP
task_handle_t asc_sleep(uint64_t ms);

//MPSC
mpsc_pair_t mpsc_new(size_t packet_size, size_t max_messages);
mpsc_rx_ref_t mpsc_move_rx(mpsc_pair_t pair);
mpsc_rx_t mpsc_take_rx(mpsc_rx_ref_t rx_reference);
mpsc_tx_t mpsc_clone_tx(mpsc_pair_t pair);
void mpsc_drop_tx(mpsc_pair_t pair);
void mpsc_drop_rx(mpsc_pair_t pair);
task_handle_t asc_mpsc_send(mpsc_tx_t tx, void* data);
task_handle_t asc_mpsc_recv(mpsc_rx_t rx, void* buff);

#endif // DEMO_ASCLIB_SYNC_H_