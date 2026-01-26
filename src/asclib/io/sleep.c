#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <time.h>
#include <assert.h>

#include <defs.h>
#include <dispatcher.h>


static uint64_t asc_get_time_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (ts.tv_nsec / 1000000);
}

void asc_sleep(uint64_t ms) {
    if (ms == 0) return;
    
    worker_t* w = tl_worker;
    assert(w);

    TCB* t = w->tasks[w->current];
    t->wake_at = asc_get_time_ms() + ms;
    t->status = WAITING;

    // Switch back to scheduler
    _asm_ctx_switch_64(&w->sched_ctx, &t->ctx);

    // return when woken up
}

void try_wake_up(TCB* candidate) {
    if (asc_get_time_ms() >= candidate->wake_at) {
        candidate->wake_at = 0;
        candidate->status = READY;
    }
}