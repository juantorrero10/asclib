#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <sys/epoll.h>
#include <errno.h>

#define __DEBUG

#include <dispatcher.h>
#include <task_control.h>
#include <task_mem.h>
#include <ascerr.h>
#include <log.h>

// Give control back
void __task_yield_impl() {
    worker_t* w = tl_worker;

    ASSERT_CONTEXT(w, "yield");

    size_t i = w->tasks.current;
    TCB* t = w->tasks.arr[i];

    if (t->status == RUNNING)
        t->status = READY;

    _asm_ctx_switch_64(
        &w->sched_ctx,
        &t->ctx
    );

    // -- resume execution from dispatcher here 
    // after the task gets picked back eventually

    return; // <-- return to task 
}

void* __task_await_impl(task_handle_t th) {
    worker_t* w = tl_worker;
    void* result;

    ASSERT_CONTEXT(w, "await()");

    // Functions can 
    if (th == HANDLE_IGNORE) { 
        __task_yield_impl();
        return calloc(1, 8);
    }
    else {
        uint64_t id = (uint64_t)th;
        ASSERT_VALID_HANDLE(w, id);
    }

    assert(w->tasks.arr[th]);


    // If by the time we've called its already finished just cleanup and yield
    if (STATUS_OF(th) == FINISHED || !_get_occupation_at(th)) {
        result = w->tasks.arr[th]->returned;
        __task_destroy(th);
        __task_yield_impl();
        return result;
    }


    size_t current_id = w->tasks.current;
    TCB* t = w->tasks.arr[current_id];
    
    t->status = WAITING;
    t->w_reason = WAIT_TASK;
    t->wait_for.task = th;

    _asm_ctx_switch_64(&w->sched_ctx, &w->tasks.arr[current_id]->ctx);

    // <-- Execution resumes here when target task finishes.
    
    if (!w->tasks.arr[th] || w->tasks.arr[th]->detached) {
        asc_error(WARN_AWAITING_DETACHED_TASK, "__task_await_impl()", (any_t)th);
        w->tasks.arr[w->tasks.current]->status = READY;
        return calloc(1, 8); //Return 0 
    }

    t = w->tasks.arr[current_id];

    t->w_reason = WAIT_NONE;

    result = w->tasks.arr[th]->returned;

    // Clean 
    __task_destroy(th);

    return result;
}

void __task_wait_on_fd(int fd, uint32_t events) {
    worker_t* w = tl_worker;
    ASSERT_CONTEXT(w, "__task_wait_on_fd");
    size_t curr = w->tasks.current;
    TCB* t = w->tasks.arr[curr];

    t->w_reason = WAIT_FD;
    t->status = WAITING;
    t->wait_for.fd = fd;


   struct epoll_event ev = { .events = events | EPOLLONESHOT, .data.u32 = (uint32_t)curr };

    // Try MOD first, if it fails, ADD. This ensures the kernel always has the right Task ID
    if (epoll_ctl(w->sync.epoll_fd, EPOLL_CTL_MOD, fd, &ev) == -1) {
        if (errno == ENOENT) {
            epoll_ctl(w->sync.epoll_fd, EPOLL_CTL_ADD, fd, &ev);
        }
    }

    __task_yield_impl();

    // After waking up;
    //t->w_reason = WAIT_NONE;
}


__attribute__((noreturn))
void task_trampoline(void) {
    

    worker_t* w = tl_worker;
    ASSERT_CONTEXT(w, "task_trampoline");
    TCB* t = w->tasks.arr[w->tasks.current];

    void* res = t->fptr((void*)t->arg);

    // Start the task!!
    if (!t->dyn_created) {
        // One of the initial tasks
        w->returned_array[w->tasks.current] = res;

        // Signal that one task has finished
        pthread_mutex_lock(&w->sync.lock);
        w->sync.initial_remaining--;
        if (w->sync.initial_remaining == 0) {
            w->sync.initial_done = 1;
            pthread_cond_broadcast(&w->sync.cond);
        }
        pthread_mutex_unlock(&w->sync.lock);

    } else {
        //Dynamically created
        t->returned = res;
    }
    t->status = FINISHED;

    

    // See if we are being waited for by another task
    for (size_t i = 0; i < w->tasks.capacity; i++)
    {
        if (w->tasks.arr[i] == NULL) continue;
        if (!_get_occupation_at(i)) continue;

        if (STATUS_OF(i) == WAITING && IS_WAITING_FOR_US(w, i)) {
            STATUS_OF(i) = READY;
        }
    }

    if (t->detached) {
        free(t->returned);
        // Cleanup on dispatcher
    }
    

    // Goodbye forever
    _asm_ctx_switch_64(
        &w->sched_ctx,
        &t->ctx
    );

    __builtin_unreachable();
}

void task_detach() {
    worker_t* w = tl_worker;
    ASSERT_CONTEXT(w, "detach");
    w->tasks.arr[w->tasks.current]->detached = 1;

    // Alert any task that he have detached
    for (size_t i = 0; i < w->tasks.capacity; i++)
    {
        if (_get_occupation_at(i) && w->tasks.arr[i] && IS_WAITING_FOR_US(w, i)) {
            w->tasks.arr[i]->status = READY;
        }
    }
}

void* __task_alloc_ret_val(size_t s, void* src) {
    void* dst = malloc(s);
    if (!dst) return NULL;
    memcpy(dst, src, s);
    return dst;
}