#ifndef ASCLIB_SYNC_H_
#define ASCLIB_SYNC_H_

#include "core.h"

typedef struct _ASCLIB_One_Shot_Channel oneshot_t;
typedef struct _ASCLIB_spsc_Channel spsc_t;


typedef struct _ASCLIB_Mpsc_rx_Handle      *mpsc_rx_t;
typedef struct _ASCLIB_Mpsc_rx_ref_Handle  *mpsc_rx_ref_t;
typedef struct _ASCLIB_Mpsc_tx_Handle      *mpsc_tx_t;
typedef struct _ASCLIB_Mpsc_pair_Handle    *mpsc_pair_t;

// SLEEP
task_handle_t asc_sleep(uint64_t ms);

//MPSC
mpsc_pair_t mpsc_new(size_t packet_size, size_t max_messages, int __track_closure);
mpsc_rx_ref_t mpsc_move_rx(mpsc_pair_t pair);
mpsc_rx_t mpsc_take_rx(mpsc_rx_ref_t rx_reference);
mpsc_tx_t mpsc_clone_tx(mpsc_pair_t pair);
mpsc_rx_ref_t mpsc_release_rx(mpsc_rx_t rx);
void mpsc_drop_tx(mpsc_tx_t pair);
void mpsc_drop_rx(mpsc_rx_t pair);
task_handle_t asc_mpsc_send(mpsc_tx_t tx, void* data);
task_handle_t asc_mpsc_recv(mpsc_rx_t rx, void* buff);
void mpsc_assert_closed(mpsc_pair_t pair);

// ERROR
#define _ASCERR_ERR_START 100
#define _ASCERR_WARN_START 200

typedef enum {
    CHAN_OK = 0,
    CHAN_PANIC_UNAUTHORIZED,     
    CHAN_PANIC_STATE_CORRUPTION, 
    CHAN_ERROR_RX_INVALID = _ASCERR_ERR_START, 
    CHAN_ERROR_RX_LOCKED,       
    CHAN_ERROR_DEAD_CHANNEL,    
    CHAN_ERROR_NULL_PTR,        
    CHAN_WARN_DOUBLE_DROP = _ASCERR_WARN_START, 
    CHAN_WARN_RETURN_NULL,      
} asclib_err_channels_t;

#endif // DEMO_ASCLIB_SYNC_H_