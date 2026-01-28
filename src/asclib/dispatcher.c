#define _POSIX_C_SOURCE 199309L

#include <unistd.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <signal.h>
#include <sys/epoll.h>



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

#ifdef __DEBUG_STATES
void log_decision_state(worker_t* w) {
    size_t spaces = 0;
    char* names[] = {"UNITIALIZED", "RUNNING", "READY", "WAITING", "FINISHED"};
    char* wait_for[] = {"WAIT_NONE", "WAIT_TASK", "WAIT_FD", "WAIT_TIMER"};
    for (size_t i = 0; i < w->tasks.capacity; i++)
    {

        if (!_get_occupation_at(i)) continue;
        if (i == w->tasks.current) printf("\033[90m[*]\033[1m\033[97m ");
        else printf("\033[90m[*] ");
        spaces = 20 - strlen(names[w->tasks.arr[i]->status]);
        printf("%zu: %s%.*s<%s -> %lu>\033[0m\n", i, names[w->tasks.arr[i]->status], (int)spaces, "                   ", 
            (w->tasks.arr[i]->status == WAITING)? wait_for[w->tasks.arr[i]->w_reason] : " _ ",
            (w->tasks.arr[i]->status == WAITING)? w->tasks.arr[i]->wait_for.timestamp : 0 );
        
    }
}
#endif


// Centralized dispatcher: Round robin
void* dispatcher(void* arg) {
    worker_t* w = (worker_t*)arg;
    assert(w);
    tl_worker = w;
    w->returned_array = calloc(w->tasks.running, sizeof(task_result));
    assert(w->returned_array);

    set_sigsegv_handler();
    
    TCB* t = NULL;

    __alloc_occupation_map();


    for (;;) {

        // Get next runnable task: Round robin
        int all_done = 1;
        int found = 0;
        
        // size_t next = (size_t)w->tasks.current + 1;

        #ifdef __DEBUG_STATES
        size_t buffer = w->tasks.current;
        log_decision_state(w);
        #endif        
        
        #ifdef __DEBUG_OCCMAP
        printf("[*] Occupation map: ");
        for (size_t i = 0; i < w->tasks.capacity; i++)
        {
            printf("%d, ", _get_occupation_at(i));
        }
        printf("\n");
        
        #endif

        // Next task election.
        for (size_t i = 0; i < w->tasks.capacity; i++)
        {
            int idx = (w->tasks.current + 1 + i) % w->tasks.capacity;    

            if (w->tasks.arr[idx] == NULL || !_get_occupation_at(idx)) continue;

            // Candidate may be sleeping
            if (SLEEPING(w->tasks.arr[idx])) {
                try_wake_up(w->tasks.arr[idx]);
            }

            if (w->tasks.arr[idx]->status != FINISHED) all_done = 0;

            if (STATUS_RUNNABLE(w->tasks.arr[idx]->status) && found == 0) {
                t = w->tasks.arr[idx];
                w->tasks.current = idx;
                found = 1;
            }
        }
        #ifdef __DEBUG_STATES
        OKAY("found=%d, all_done=%d, previous=%zu, current=%zu", found, all_done, buffer, w->tasks.current);
        #endif

        if (all_done) {
            break; // we're done
        }
        
        // All tasks are waiting for something/someone:
        // In this case we need to poll -> lazy wait but wake up the instant data is ready.
        if (!found) {
            int n_ready = epoll_wait(w->sync.epoll_fd, w->sync.ready_events, MAX_EVENTS, 100);

            for (int i = 0; i < n_ready; i++)
            {
                size_t wake = w->sync.ready_events[i].data.u32;
                if (w->tasks.arr[wake]) {
                    w->tasks.arr[wake]->w_reason = WAIT_NONE;
                    w->tasks.arr[wake]->wait_for.fd = -1;
                    w->tasks.arr[wake]->status = READY;
                }
            }
            
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
            asc_error(PANIC_STACK_OVERFLOW_RSP_BOUND, "dispatcher()", (any_t)(void*)t->ctx.rsp);
        }

        if (STATUS_OF(w->tasks.current) == FINISHED) {
            #ifdef __DEBUG_DESTROY
            INFO("TASK<%zu> destroy", w->tasks.current);
            #endif
            if (w->tasks.arr[w->tasks.current]->detached) {
                __task_destroy(w->tasks.current);
                w->tasks.arr[w->tasks.current] = NULL;
            }
            continue;
        }
    }


    int leaked_tasks = 0;
    for (size_t i = w->tasks.initial; i < w->tasks.capacity; i++) {
        if (_get_occupation_at(i) && !w->tasks.arr[i]->detached) { // Slot is still active
            leaked_tasks++;
        }
    }
    

    if (leaked_tasks) {
        asc_error(ERR_TASK_OUTLIVED_FATHER, "dispatcher()", (any_t)leaked_tasks);
    }

    //Free the TLS objects
    __free_occupation_map();
    __io_free_hashmap();

    return NULL;
} 
