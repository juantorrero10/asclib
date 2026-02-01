
#include <stdio.h>
#include "asclib/core.h"
#include "asclib/sync.h"


async(task, oneshot_tx_t tx) {
    await(asc_sleep(2000));

    int p = 4;

    await(asc_oneshot_send(arg(tx), &p));

    task_return_void();
}


async(main_task) {

    int buffer;

    await(asc_sleep(100));
    
    oneshot_pair_t os = oneshot_new(4, 1);

    task_spawn(task, .tx = (oneshot_tx_t)os);

    if (await_ret(asclib_err_channels_t, asc_oneshot_recv((oneshot_rx_t)os, &buffer)) == CHAN_OK) {
        printf("Finished ok! {%d}\n", buffer);
    }

    oneshot_free(&os);

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