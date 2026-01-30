#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <time.h>
#include <assert.h>

#include <defs.h>
#include <dispatcher.h>
#include <task_mem.h>
#include <task_control.h>
#include <collections.h>


#define ENABLE_GUARD_PAGE 1

TLS heap_t* timers = NULL;

uint64_t get_time_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (ts.tv_nsec / 1000000);
}

// Set wake up timer and return to dispatcher
void* __internal_async_sleep(void* __arg) {
    uint64_t ms = *((uint64_t*)&__arg);


    assert(timers);

    worker_t* w = tl_worker;
    TCB* t = w->tasks.arr[w->tasks.current];

    t->wait_for.timestamp = get_time_ms() + ms;
    t->status = WAITING;
    t->w_reason = WAIT_TIMER;

    heap_push(w, timers, w->tasks.current);

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

    task_entry_t te = {
        .fptr = __internal_async_sleep,
        .value = (void*)ms,
        .arg_size = sizeof(void*),
        .n_pages = 0,
        .arg_malloced = 0
    };
    
    // Create concurrent task, without page guard and tiny stack.
    return __task_create_ex(te, 4096, ENABLE_GUARD_PAGE);
    
}



void try_wake_up(TCB* candidate) {

    if (candidate->w_reason == WAIT_TIMER && get_time_ms() >= candidate->wait_for.timestamp) {
        candidate->wait_for.timestamp = 0;
        candidate->w_reason = WAIT_NONE;
        candidate->status = READY;
    }
}

