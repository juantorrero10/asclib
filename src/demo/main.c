#include <stdio.h>


#include "asclib/core.h"
#include "asclib/sync.h"


async(slow_consumer, mpsc_rx_ref_t rx_ref) {
    mpsc_rx_t rx = mpsc_take_rx(arg(rx_ref));
    int buf;
    // Wait a bit to let producers fill the buffer
    await(asc_sleep(2000)); 
    for(int i = 0; i < 9; i++) {
        await(asc_mpsc_recv(rx, &buf));
        printf("[Consumer] Popped: %d\n", buf);
        await(asc_sleep(100)); // Slow consumption
    }

    task_return(mpsc_rx_ref_t, mpsc_release_rx(rx););
}

async(fast_producer, mpsc_tx_t tx; int id) {
    detach;
    int j = 0;
    for(int i = 0; i < 5; i++) {
        printf("[P%d] Sending %d...\n", arg(id), (arg(id) * 10) + i);
        j = (i + (arg(id) * 10));
        await(asc_mpsc_send(arg(tx), &j));
        printf("[P%d] Sent %d (didn't block)\n", arg(id), i);
    }
    // This 6th one SHOULD block because capacity is 5
    int val = 999;
    printf("[P%d] Sending 6th (expecting block)...\n", arg(id));
    await(asc_mpsc_send(arg(tx), &val));
    printf("[P%d] 6th Sent! (unblocked)\n", arg(id));
    
    mpsc_drop_tx(arg(tx));
    task_return_void();
}

async(main_task) {

    mpsc_pair_t pair = mpsc_new(sizeof(int), 5, 1);

    mpsc_rx_t rx = mpsc_take_rx(await_all( 
        task_spawn(slow_consumer, .rx_ref = mpsc_move_rx(pair)),
        task_spawn(fast_producer, .tx = mpsc_clone_tx(pair), .id = 1),
        task_spawn(fast_producer, .tx = mpsc_clone_tx(pair), .id = 2), 
    ).get(0));
    int number = 0;

    await(asc_mpsc_recv(rx, &number));
    printf("[Main task]: Popped %d\n", number);
    mpsc_drop_rx(rx);
    
    mpsc_assert_closed(pair);
    task_return_void();
}

int main() {
    // For main_task (no params), we use the old entry style or update it too

    worker_t* w = worker_create(1, task_entry(main_task));

    worker_run(w);
    printf("Returned!\n");
    worker_await_all(w);
    worker_destroy(w);
    printf("End program!!\n");
    return 0;
}