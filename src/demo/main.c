#include <stdio.h>


#include "asclib/core.h"
#include "asclib/sync.h"


async(producer, mpsc_tx_t tx; int id) {

    printf("[producer%d]: ready to send data\n", arg(id));
    for (size_t i = 0; i < 10; i++)
    {
        await(asc_sleep(1000));
        await(asc_mpsc_send( arg(tx), &arg(id) ));    
    }
    
    mpsc_drop_tx(arg(tx));
    printf("[producer%d]: closed connection.\n", arg(id));
    task_return_void();
}

async(consumer, mpsc_rx_ref_t rx) {

    mpsc_rx_t rx = mpsc_take_rx(arg(rx));
    int buffer;
    printf("[Consumer]: waiting for data to arrive\n");
    for (size_t i = 0; i < 20; i++)
    {
        await(asc_mpsc_recv(rx, &buffer));
        printf("[Consumer]: received: %d", buffer);
    }
    mpsc_drop_rx(rx);
    printf("[Consumer]: closed connection\n.");
    task_return_void();
}

async(main_task, int value; const char* val) {

    mpsc_pair_t pair = mpsc_new(sizeof(int), 20);

    task_handle_t t1 = task_spawn(consumer, .rx = mpsc_move_rx(pair));
    task_handle_t t2 = task_spawn(producer, .tx = mpsc_clone_tx(pair), .id = 101);
    task_handle_t t3 = task_spawn(producer, .tx = mpsc_clone_tx(pair), .id = 102);

    await(t1);
    await(t2);
    await(t3);

    task_return_void();
}

int main() {
    // For main_task (no params), we use the old entry style or update it too

    worker_t* w = worker_create(1, task_entry(main_task, .value = 2, .val = "String."));

    worker_run(w);
    worker_await(w);

    printf("Returned!\n");
    worker_destroy(w);
    printf("End program!!\n");
    return 0;
}