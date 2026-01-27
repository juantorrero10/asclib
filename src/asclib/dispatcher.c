#define _POSIX_C_SOURCE 199309L

#include <unistd.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <signal.h>

//#define __DEBUG
//#define __DEBUG_DESTROY

#include <dispatcher.h>
#include <log.h>
#include <task_control.h>
#include <task_mem.h>
#include <ascerr.h>
#include <task_stack.h>
#include <asc_sleep.h>


// Thread-local worker variable
TLS worker_t* tl_worker = NULL;

void print_ctx(ctx_t* c) {
    printf("CONTEXT:\n");
    char* names[] = {"rsp", "rbp", "rbx", "r12", "r13", "r14", "r14"};
    for (size_t i = 0; i < 7; i++)
    {
        printf("%s: %p", names[i], ((uint8_t**)c)[i]);
        if ((i == 1 || i == 0) && ((uint8_t**)c)[i] != 0) printf(" -> 0x%012lx", *((uint64_t*)((uint8_t**)c)[i]));
        printf("\n");
    }
    
}



// Centralized dispatcher: Round robin
void* dispatcher(void* arg) {
    worker_t* w = (worker_t*)arg;
    assert(w);
    tl_worker = w;
    w->returned_array = calloc(w->n_tasks, sizeof(task_result));
    assert(w->returned_array);

    set_sigsegv_handler();
    
    TCB* t = NULL;

    __alloc_occupation_map();

    for (;;) {

        // Get next runnable task: Round robin
        int all_finished = 1;
        int found = 0;
        
        // size_t next = (size_t)w->current + 1;

        #ifdef __DEBUG_STATES
        size_t buffer = w->current;
        char* names[] = {"UNITIALIZED", "RUNNING", "READY", "WAITING", "FINISHED"};
        for (size_t i = 0; i < w->n_tasks; i++)
        {
            INFO("%zu: %s", i, names[w->tasks[i]->status]);
        }
        #endif        
        
        #ifdef __DEBUG_OCCMAP
        INFO("Occupation map:");
        for (size_t i = 0; i < w->capacity; i++)
        {
            printf("%d, ", _get_occupation_at(i));
        }
        
        #endif
        for (size_t i = 0; i < w->capacity; i++)
        {
            int idx = (w->current + 1 + i) % w->capacity;    

            if (w->tasks[idx] == NULL || !_get_occupation_at(idx)) continue;

            // Candidate may be sleeping
            if (SLEEPING(w->tasks[idx])) {
                try_wake_up(w->tasks[idx]);
            }

            if (w->tasks[idx]->status != FINISHED) all_finished = 0;

            if (STATUS_RUNNABLE(w->tasks[idx]->status) && found == 0) {
                t = w->tasks[idx];
                w->current = idx;
                found = 1;
            }
        }
        #ifdef __DEBUG_STATES
        OKAY("found=%d, all_finished=%d, previous=%zu, current=%zu", found, all_finished, buffer, w->current);
        #endif

        if (all_finished) {
            break; // we're done
        }
        
        if (!found) {
            // Everyone is WAITING, but nobody is READY.
            // Sleep for 100 microseconds to save CPU power.
            struct timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = 100000; // 100,000 nanoseconds = 100 microseconds
            
            nanosleep(&ts, NULL);
            continue;
        }

        if (t->status == UNITIALIZED) {
            // Create frame
            create_stack_frame(t->stack_size, t, t->guard_page);
        }
        // If unitialized this'll go to task_trampoline.
        // Else it will go to __task_yield_impl, after the context switch
        t->status = RUNNING;
        
        _asm_ctx_switch_64(
            &t->ctx,
            &w->sched_ctx
        );

        // Returned from a task, check stack overflow by bounds (second-layer protection).
        // 128 shadow bytes
        if (t->ctx.rsp < t->stack + 128) {
            asc_error(PANIC_STACK_OVERFLOW_RSP_BOUND, "dispatcher()", (int64_t)t->ctx.rsp);
        }

        if (STATUS_OF(w->current) == FINISHED) {
            #ifdef __DEBUG_DESTROY
            INFO("TASK<%zu> destroy", w->current);
            #endif
            if (w->tasks[w->current]->detached) {
                __task_destroy(w->current);
                w->tasks[w->current] = NULL;
            }
            continue;
        }
    }


    int leaked_tasks = 0;
    for (size_t i = w->initial_tasks; i < w->capacity; i++) {
        if (_get_occupation_at(i) && !w->tasks[i]->detached) { // Slot is still active
            leaked_tasks++;
        }
    }
    

    if (leaked_tasks) {
        asc_error(ERR_TASK_OUTLIVED_FATHER, "dispatcher()", leaked_tasks);
    }

    __free_occupation_map();

    return NULL;
} 
