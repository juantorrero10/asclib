#define _POSIX_C_SOURCE 199309L

#include <unistd.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <signal.h>
#include <sys/epoll.h>

//#define __DEBUG
//#define __DEBUG_STATES
//#define __DEBUG_OCCMAP
//#define __DEBUG_TIMERS
//#define __DEBUG_CTX

#include <dispatcher.h>
#include <log.h>
#include <task_control.h>
#include <task_mem.h>
#include <ascerr.h>
#include <task_stack.h>
#include <runtime.h>
#include <collections.h>

// Managed by sleep.c
#define TIMERS_INITIAL_CAP 16
extern TLS timer_heap_t* timers;

// Managed by task_control.c
extern TLS uu_hash_map* hmawait;

// Managed by task_mem.c
extern TLS g_arrlist_t* generation_map;

// Thread-local worker variable
TLS worker_t* tl_worker = NULL;


#ifdef __DEBUG_CTX
static void print_ctx(ctx_t* c) {
    printf("CONTEXT:\n");
    char* names[] = {"rsp", "rbp", "rbx", "r12", "r13", "r14", "r14"};
    for (size_t i = 0; i < 7; i++)
    {
        printf("%s: %016lx", names[i], (uint64_t)((uint8_t**)c)[i]);
        if ((i == 1 || i == 0) && ((uint8_t**)c)[i] != 0) printf(" -> 0x%012lx", *((uint64_t*)((uint8_t**)c)[i]));
        printf("\n");
    }
    
}
#endif
#ifdef __DEBUG_STATES
static void log_decision_state(worker_t* w, char* buff) {
    size_t spaces = 0;
    char* names[] = {"UNITIALIZED", "RUNNING", "READY", "WAITING", "FINISHED"};
    char* wait_for[] = {"WAIT_NONE", "WAIT_TASK", "WAIT_FD",  "WAIT_CHANNEL", "WAIT_TIMER"};
    size_t sz = 0;
    uint64_t num_to_show = 0;
    for (size_t i = 0; i < w->tasks.capacity; i++)
    {

        if (!_get_tcbstate_at(i)) continue;
        if (i == w->tasks.current) { 
            sz += sprintf(buff + sz, "\033[90m[*]\033[1m\033[97m "); 
        }
        else sz += sprintf(buff + sz, "\033[90m[*] ");

        if (IS_VIRTUAL(i)) {
            sz += sprintf(buff + sz, "%zu: VIRTUAL\n", i);
        } else {
            if (w->tasks.arr[i]->status == WAITING && w->tasks.arr[i]->w_reason == WAIT_TIMER) {
                num_to_show = w->tasks.arr[i]->wait_for.timestamp - get_time_ms();
            } else {
                if (w->tasks.arr[i]->w_reason == WAIT_TASK) num_to_show &= 0xFFFFFFFF;
                num_to_show = w->tasks.arr[i]->wait_for.timestamp;
            }
            spaces = 20 - strlen(names[w->tasks.arr[i]->status]);
            sz += sprintf(buff + sz, "%zu: %s%.*s<%s -> %lu>\033[0m\n", i, names[w->tasks.arr[i]->status], (int)spaces, "                   ", 
            (w->tasks.arr[i]->status == WAITING)? wait_for[w->tasks.arr[i]->w_reason] : " _ ",
            (w->tasks.arr[i]->status == WAITING)? num_to_show : 0 );
        }
        
        
    }
}
#endif


static void lazy_wait(worker_t* w) {
    // Calculate the next timer to wake up
    int64_t timeout_ms = -1; // Default: block indefinitely if no timers
    uint64_t now = get_time_ms();
    size_t top;

    assert(timers);

    // Erase invalid entries
    while (timers->size > 0) {
        top = timers->data[0].th; // Looking-ahead, we don't want to modify a valid entry
        size_t idx = LOWER_32(top);
        if (!TCB_AT(idx) || STATUS_OF(idx) == FINISHED || W_REASON(idx) != WAIT_TIMER || IS_STALE(top)) {
            heap_pop(timers);
            continue;
        }
        break;
    }

    #ifdef __DEBUG_TIMERS
    for (size_t i = 0; i < timers->size; i++)
    {
        if (TCB_AT(LOWER_32(timers->data[i].th)))
            OKAY("T[%zu]: %lx (%lu ms)", i, timers->data[i].th, timers->data[i].timeout - now);
        else
            WARN("T[%zu]: %lx", i, timers->data[i].th);
    }
    #endif


    if (timers->size > 0) {
        uint64_t next_timer = timers->data[0].timeout;

        if (next_timer <= now) {
            timeout_ms = 0; // Already expired
        } else {
            timeout_ms = (int)(next_timer - now);
        }
    }

    // 2. Sleep until I/O OR the next timer
    int n_ready = epoll_wait(w->sync.epoll_fd, w->sync.ready_events, MAX_EVENTS, timeout_ms);

    // 3. Handle I/O wakeups
    for (int i = 0; i < n_ready; i++) {
        size_t wake = w->sync.ready_events[i].data.u64;
        size_t wake_l32 = LOWER_32(wake);
        if (w->tasks.arr[wake_l32] && !IS_STALE(wake)) {
            w->tasks.arr[wake_l32]->w_reason = WAIT_NONE;
            w->tasks.arr[wake_l32]->status = READY;
        }
    }
}


// Centralized dispatcher: Round robin
void* dispatcher(void* arg) {
    worker_t* w = (worker_t*)arg;
    assert(w);
    tl_worker = w;
    w->returned_array = calloc(w->tasks.running, sizeof(task_result_t));
    assert(w->returned_array);
    

    //set_sigsegv_handler();
    hmawait = NULL;
    
    TCB* t = NULL;
    TCB* t2 = NULL; (void)t2;

    __alloc_occupation_map();
    if (!generation_map) {
        generation_map = gal_alloc(w->tasks.initial, sizeof(uint32_t));
        uint32_t one = 1;
        for (size_t i = 0; i < w->tasks.initial; i++)
        {
            gal_append(generation_map, &one);
        }
        
    }
    timers = heap_alloc(TIMERS_INITIAL_CAP);


    #ifdef __DEBUG_STATES
    char states[1024];
        struct {
        size_t curr;
        size_t prev;
        task_status st;
    } last_dec;
    #endif

    for (;;) {

        // Get next runnable task: Round robin
        int all_done = 1;
        int found = 0;
        
        // size_t next = (size_t)w->tasks.current + 1;

        #ifdef __DEBUG_STATES
        size_t buffer = w->tasks.current;
        log_decision_state(w, states);
        #endif        
        int idx = 0;

        // Next task election.
        for (size_t i = 0; i < w->tasks.capacity; i++)
        {
            idx = (w->tasks.current + 1 + i) % w->tasks.capacity;
            
            if (!TCB_AT(idx)) continue;
            
            // Candidate may be sleeping
            if (SLEEPING(w->tasks.arr[idx])) {
                try_wake_up(w->tasks.arr[idx]);
            }

            if (w->tasks.arr[idx]->status != FINISHED) all_done = 0;

            if (STATUS_RUNNABLE(w->tasks.arr[idx]->status) && found == 0) {
                t = w->tasks.arr[idx];
                w->tasks.current = idx;
                found = 1;
                //break;
            }
        }
        #ifdef __DEBUG_STATES
        t2 = w->tasks.arr[w->tasks.current];
        task_status ts = last_dec.st;
        if (TCB_AT(w->tasks.current)) ts = STATUS_OF(w->tasks.current);
        if (last_dec.curr != w->tasks.current || last_dec.st != ts || last_dec.prev != buffer){
            printf("%s", states);
            OKAY("found=%d, all_done=%d, previous=%zu, current=%zu, remaining=%zu, running=%zu\n", found, all_done, buffer, w->tasks.current, w->sync.initial_remaining, w->tasks.running);
        }
        last_dec.curr = w->tasks.current;
        if (TCB_AT(w->tasks.current))last_dec.st = STATUS_OF(w->tasks.current);
        last_dec.prev = buffer;
        #endif

        #ifdef __DEBUG_OCCMAP
        printf("[*] Occupation map: ");
        for (size_t i = 0; i < w->tasks.capacity; i++)
        {
            printf("%d, ", _get_tcbstate_at(i));
        }
        printf("\n");
        
        #endif

        if (all_done) {
            break; // we're done
        }
        
        // All tasks are waiting for something/someone:
        // In this case we need to poll -> lazy wait but wake up the instant data is ready.
        if (!found) {
            lazy_wait(w);
            continue;
        }

        if (t->status == UNITIALIZED) {
            // Create frame
            create_stack_frame(t->stack_size, t, t->guard_page);
        }
        // If unitialized this'll go to task_trampoline.
        // Else it will go to __task_yield_impl, after the context switch
        t->status = RUNNING;
        #ifdef __DEBUG_CTX
        INFO("TASK%zu ctx", tl_worker->tasks.current);
        print_ctx(&t->ctx);
        INFO("SCHED ctx");
        print_ctx(&w->sched_ctx);
        #endif
        
    
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
        if (TCB_AT(i) && !w->tasks.arr[i]->detached && STATUS_OF(i) != FINISHED) { // Slot is still active
            leaked_tasks++;
        }
    }

    if (leaked_tasks) {
        asc_error(ERR_TASK_OUTLIVED_FATHER, "dispatcher()", (any_t)leaked_tasks);
    }

    //Free the (Thread)LS objects
    __free_taskmem_objs();
    __io_free_fdmap();
    __hm_await_free();
    heap_dealloc(&timers);

    return NULL;
} 
