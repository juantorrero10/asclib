#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <time.h>
#include <assert.h>

#include <defs.h>
#include <dispatcher.h>
#include <task_mem.h>
#include <task_control.h>


static uint64_t asc_get_time_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (ts.tv_nsec / 1000000);
}

// Set wake up timer and return to dispatcher
void* __internal_async_sleep(void* __arg) {
    uint64_t ms = *((uint64_t*)&__arg);

    
    worker_t* w = tl_worker;
    TCB* t = w->tasks.arr[w->tasks.current];

    t->wait_for.timestamp = asc_get_time_ms() + ms;
    t->status = WAITING;
    t->w_reason = WAIT_TIMER;

    __task_yield_impl();

    return NULL;
}

/**
 * @brief create non-blocking sleep task
 * @param ms Milliseconds to-sleep
 * @note use await() to block current task until time its over
 */
task_handle_t asc_sleep(uint64_t ms) {
    if (ms == 0) return -1;
    
    // Create concurrent task, without page guard and tiny stack.
    return __task_create_ex(
        task_entry_ex(__internal_async_sleep, uint64_t, ms, 0), 512, 0);
    
}



void try_wake_up(TCB* candidate) {

    if (candidate->w_reason == WAIT_TIMER && asc_get_time_ms() >= candidate->wait_for.timestamp) {
        candidate->wait_for.timestamp = 0;
        candidate->w_reason = WAIT_NONE;
        candidate->status = READY;
    }
}

