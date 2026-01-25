#include <assert.h>
#include <string.h>
#include <stdio.h>

#include <log.h>
#include <dispatcher.h>
#include <task_control.h>

// Give control back
void task_yield() {
    worker_t* w = tl_worker;

    assert(w != NULL);

    size_t i = w->current;
    TCB* t = w->tasks + i;
    t->status = READY;

    _asm_ctx_switch_64(
        &w->sched_ctx,
        &t->ctx
    );

    //INFO("Task%d: Picked back", w->current);
    // -- resume execution from dispatcher here 
    // after the task gets picked back eventually

    return; // <-- return to task 
}


void* __task_await_impl(task_handle_t th) {
    worker_t* w = tl_worker;
    assert(w);
    assert(th < w->n_tasks);

    size_t i = w->current;
    STATUS_OF(i) = WAITING;
    w->tasks[i].waiting_for = th;

    TCB* t = &w->tasks[i];

    //Switch back to dispatcher
    _asm_ctx_switch_64(
        &w->sched_ctx,
        &t->ctx
    );

    // We will get picked up here!!
    assert(STATUS_OF(th) == FINISHED);
    return w->returned_array[th];

}


__attribute__((noreturn))
void task_trampoline(void) {

    
    worker_t* w = tl_worker;
    assert(tl_worker != NULL);
    TCB* t = &w->tasks[w->current];

    // Start the task!!
    w->returned_array[w->current] = t->fptr((void*)t->arg);
    t->status = FINISHED;


    // See if we are being waited for by another task
    for (size_t i = 0; i < w->n_tasks; i++)
    {
        if (STATUS_OF(i) == WAITING && WAITING_FOR(i) == w->current) {
            STATUS_OF(i) = READY;
        }
    }
    
    
    // Goodbye forever
    _asm_ctx_switch_64(
        &w->sched_ctx,
        &t->ctx
    );

    __builtin_unreachable();
}


void* __task_alloc_ret_val(size_t s, void* v) {
    void* dst = malloc(s);
    memcpy(dst, &v, s);
    return dst;
}