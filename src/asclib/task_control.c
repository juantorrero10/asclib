#include <assert.h>
#include <string.h>
#include <stdio.h>


#include <dispatcher.h>
#include <task_control.h>
#include <task_mem.h>
#include <ascerr.h>
//#define __DEBUG
#include <log.h>

// Give control back
void task_yield() {
    worker_t* w = tl_worker;

    assert(w != NULL);

    size_t i = w->current;
    TCB* t = w->tasks[i];
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
    size_t current_id = w->current;
    
    w->tasks[current_id]->status = WAITING;
    w->tasks[current_id]->waiting_for = th;

    _asm_ctx_switch_64(&w->sched_ctx, &w->tasks[current_id]->ctx);

    // <-- Execution resumes here when target task finishes.
        
    //After waking up
    if (!w->tasks[th] || w->tasks[th]->detached) {
        asc_error(WARN_AWAITING_DETACHED_TASK, "__task_await_impl()", th);
        w->tasks[w->current]->status = READY;
        return calloc(1, 8); //Return 0 
    }


    void* result = w->tasks[th]->returned;

    // Clean 
    __task_destroy(th);

    return result;
}


__attribute__((noreturn))
void task_trampoline(void) {
    
    INFO("Task tramp: %zu", tl_worker->current);

    worker_t* w = tl_worker;
    assert(tl_worker != NULL);
    TCB* t = w->tasks[w->current];

    void* res = t->fptr((void*)t->arg);

    // Start the task!!
    if (!t->dyn_created) {
        // One of the initial tasks
        w->returned_array[w->current] = res;

        // Signal that one task has finished
        pthread_mutex_lock(&w->sync_lock);
        w->initial_remaining--;
        if (w->initial_remaining == 0) {
            w->initial_done = 1;
            pthread_cond_broadcast(&w->sync_cond);
        }
        pthread_mutex_unlock(&w->sync_lock);

    } else {
        //Dynamically created
        t->returned = res;
    }
    t->status = FINISHED;

    

    // See if we are being waited for by another task
    for (size_t i = 0; i < w->capacity; i++)
    {
        if (w->tasks[i] == NULL) continue;
        if (!_get_occupation_at(i)) continue;

        if (STATUS_OF(i) == WAITING && WAITING_FOR(i) == w->current) {
            STATUS_OF(i) = READY;
        }
    }

    if (t->detached) {
        free(t->returned);
        __task_destroy(w->current);
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
    assert(w->tasks[w->current]);
    w->tasks[w->current]->detached = 1;

    // Alert any task that he have detached
    for (size_t i = 0; i < w->capacity; i++)
    {
        if (_get_occupation_at(i) && w->tasks[i] && w->tasks[i]->waiting_for == w->current) {
            w->tasks[i]->status = READY;
        }
    }
}

void* __task_alloc_ret_val(size_t s, void* src) {
    void* dst = malloc(s);
    if (!dst) return NULL;
    memcpy(dst, src, s);
    return dst;
}