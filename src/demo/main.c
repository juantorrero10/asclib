
#include <stdio.h>
#include "asclib/core.h"
#include "asclib/sync.h"
#include "asclib/io.h"

#include <string.h>


async(producer, mpsc_tx_t tx; int id) {

    int data = 0;
    
    for (size_t i = 0; i < 5; i++)
    {
        data = (arg(id) * 10) + i;
        printf("[P%d]: Sending -> <%d>\n", arg(id), data);
        await(asc_mpsc_send(arg(tx), &data));
    }
    
    data = (arg(id) * 10) + 5;
    printf("[P%d]: Sending -> <%d> (Expect block)\n", arg(id), data);
    await(asc_mpsc_send(arg(tx), &data));
    mpsc_drop_tx(arg(tx));
    printf("[P%d]: Closed connection!\n", arg(id));

    end_return_void();
}

async(consumer, mpsc_rx_ref_t rx) {

    await(asc_sleep(2000));
    mpsc_rx_t rx = mpsc_take_rx(arg(rx));
    int buffer;
    for (size_t i = 0; i < 8; i++)
    {
        await(asc_mpsc_recv(rx, &buffer));
        printf("[Consumer]: Popped: %d\n", buffer);
    }
    mpsc_drop_rx(rx);
    end_return_void();
}

async(main_task) {

    mpsc_pair_t pair = mpsc_new(4, 10, 1);

    task_handle_t t1 = task_spawn(consumer, .rx = mpsc_move_rx(pair));

    await_all(
        task_spawn(producer, .tx = mpsc_clone_tx(pair), .id = 1),
        task_spawn(producer, .tx = mpsc_clone_tx(pair), .id = 2)
    ).none();

    await(t1);

    mpsc_assert_closed(pair);
    end_return_void();
}

int main() {
    // For main_task (no params), we use the old entry style or update it too

    worker_t* w = worker_create(1, task_entry(main_task));

    worker_run(w);
    worker_await_all(w);
    printf("Returned!\n");
    worker_destroy(w);
    return 0;
}