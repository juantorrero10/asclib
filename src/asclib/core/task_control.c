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
#include <wrappers.h>
#include <collections.h>

// 'Methods' in c.
TLS any_result* last_any_result = NULL;
TLS all_result* last_all_result = NULL;

// Redundant but more efficient structure to store await dependencies.
#define HM_INITIAL_CAPACITY 4
TLS uu_hash_map* hmawait = NULL;

static void* __any_result_select_method();
static void* __all_result_get_method();
static void __all_result_none_method();

// Clean start after a UNITIALIZED task gets picked up by dispatcher
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
        w->tasks.running--;
        if (w->sync.initial_remaining == 0) {
            w->sync.initial_done = 1;
            pthread_cond_broadcast(&w->sync.cond);
        }
        pthread_mutex_unlock(&w->sync.lock);

    } else {
        //Task don't clean itself from the trampoline
        t->returned = res;
    }
    t->status = FINISHED;
    

    // See if we are being waited for by another task, by the awaiting group.
    if (t->await_group != (size_t)-1) {
        uint64_t old = 0;
        switch (_get_tcbstate_at(t->await_group)) {
            case VIRTUAL_RACE:
            case TCB_STATE:
                for (ssize_t i = hmawait->highest_index; i >= 0; i--)
                {
                    if (!hmawait->occ[i]) continue;
                    if (hmawait->map[i].v == t->await_group && STATUS_OF(hmawait->map[i].k) != FINISHED) {
                        STATUS_OF(hmawait->map[i].k) = READY;
                    }
                } break;

            case VIRTUAL_JOINED:
                // Substract 1 from the joined depedency
                old = (uintptr_t)w->tasks.arr[t->await_group];
                old--;
                w->tasks.arr[t->await_group] = (TCB*)old;

                // if none remaining ready up all waiters
                if (w->tasks.arr[t->await_group] == NULL) {
                    for (ssize_t i = hmawait->highest_index; i >= 0; i--)
                    {
                        if (!hmawait->occ[i]) continue;
                        if (hmawait->map[i].v == t->await_group && STATUS_OF(hmawait->map[i].k) != FINISHED) {
                            STATUS_OF(hmawait->map[i].k) = READY;
                    }
                    } 
                }break;
                
                

            default:
                assert(0);
                break;
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

void* __task_await_impl(task_handle_t th, int expect) {
    worker_t* w = tl_worker;
    void* result;

    if (th == (task_handle_t)-1) return NULL;

    if (!hmawait) {
        hmawait = hm_alloc(HM_INITIAL_CAPACITY);
    }

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
    if (!TCB_AT(th)||STATUS_OF(th) == FINISHED) {
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

    assert(hm_add_entry(hmawait, w->tasks.current, th));

    // Set the waiting group to itself.
    w->tasks.arr[th]->await_group = th;

    _asm_ctx_switch_64(&w->sched_ctx, &w->tasks.arr[current_id]->ctx);

    // <-- Execution resumes here when target task finishes.
    
    if (!TCB_AT(th) || w->tasks.arr[th]->detached) {
        asc_error(WARN_AWAITING_DETACHED_TASK, "__task_await_impl()", (any_t)th);
        w->tasks.arr[w->tasks.current]->status = READY;
        return calloc(1, 8); //Return 0 
    }

    t = w->tasks.arr[current_id];

    t->w_reason = WAIT_NONE;

    result = w->tasks.arr[th]->returned;

    if (expect && !result) {
        asc_error(PANIC_EXPECT_NULL, "__task_await_impl()", (any_t)th);
    }

    // Clean 
    __task_destroy(th);

    assert(hm_remove_entry(hmawait, w->tasks.current));

    return result;
}

any_result __task_await_any_impl(task_handle_t* handles, size_t num_task) {
    worker_t* w = tl_worker;
    ASSERT_CONTEXT(w, "__task_await_any_impl");
    void* result;

    if (!hmawait) {
        hmawait = hm_alloc(HM_INITIAL_CAPACITY);
    }

    any_result ret = { 0 };
    ret.select = __any_result_select_method;
    ret.losers = malloc(sizeof(task_handle_t) * (num_task - 1));

    task_entry_t tvir = {
        .fptr = NULL,
        .arg_malloced = 0,
        .n_pages = 0
    };
    
    //Create a virtual task
    task_handle_t virtual = __task_create_ex(
        tvir,
        0,
        0
    );

    // Set the state to race.
    SET_VIRTUAL_RACE(virtual);

    int already_finished = -1;

    // Set the await groups
    for (size_t i = 0; i < num_task; i++) {
        ASSERT_VALID_HANDLE(w, handles[i]);
        if (STATUS_OF(handles[i]) == FINISHED) { already_finished = handles[i]; break; }
        w->tasks.arr[handles[i]]->await_group = virtual;
    }

    if (already_finished != -1) {
        result = w->tasks.arr[already_finished]->returned;
        __task_destroy(already_finished);
        __task_yield_impl();
        ret.winner_return = result;
        ret.winner = already_finished;
        return ret;
    }

    // Set the status for waiting the virtual task.
    w->tasks.arr[w->tasks.current]->w_reason = WAIT_TASK;
    w->tasks.arr[w->tasks.current]->status = WAITING;
    w->tasks.arr[w->tasks.current]->wait_for.task = virtual;

    assert(hm_add_entry(hmawait, w->tasks.current, virtual));

    _asm_ctx_switch_64(&w->sched_ctx, &w->tasks.arr[w->tasks.current]->ctx);

    // Search the one that finished and clean up
    size_t winner = (size_t)-1;
    size_t n_losers = 0;
    for (size_t i = 0; i < num_task; i++)
    {
        ASSERT_VALID_HANDLE(w, handles[i]);

        // Asign to winner the first finished tasks it sees.
        if (STATUS_OF(handles[i]) == FINISHED) 
        { 
                if (winner == (size_t)-1)
                    winner = handles[i];  
        } else {
            // Detach the losers
            w->tasks.arr[handles[i]]->detached = 1;
            // Add the losers to the list
            ret.losers[n_losers++] = handles[i];
        }
        w->tasks.arr[handles[i]]->await_group = (size_t)-1;
        
    }

    assert(winner != (size_t)-1);

    ret.num_losers = n_losers;
    ret.winner = winner;

    if (!TCB_AT(winner) || w->tasks.arr[winner]->detached) {
        asc_error(WARN_AWAITING_DETACHED_TASK, "__task_await_impl()", (any_t)winner);
        w->tasks.arr[w->tasks.current]->status = READY;
        ret.winner_return = calloc(1, 8);
        return ret; 
    }

    result = w->tasks.arr[winner]->returned;

    __task_destroy(winner);
    __task_destroy(virtual);

    w->tasks.arr[w->tasks.current]->w_reason = WAIT_NONE;
    w->tasks.arr[w->tasks.current]->wait_for.task = 0;

    ret.winner_return = result;

    last_any_result = malloc(sizeof(any_result));
    memcpy(last_any_result, &ret, sizeof(any_result));

    assert(hm_remove_entry(hmawait, w->tasks.current));

    return ret;
    
}

all_result __task_await_all_impl(task_handle_t* handles, size_t num_task) {
    worker_t* w = tl_worker;
    ASSERT_CONTEXT(w, "__task_await_any_impl");


    if (!hmawait) {
        hmawait = hm_alloc(HM_INITIAL_CAPACITY);
    }

    //Create virtual tasks
    task_handle_t virtual = __task_create_ex(
        (task_entry_t){ .fptr = NULL, .arg_malloced = 0, .n_pages = 0 },
        0,
        0
    );

    // Set to joined
    SET_VIRTUAL_JOINED(virtual);

    // Wait for virtual task.
    w->tasks.arr[w->tasks.current]->status = WAITING;
    w->tasks.arr[w->tasks.current]->w_reason = WAIT_TASK;
    w->tasks.arr[w->tasks.current]->wait_for.task = virtual;

    assert(hm_add_entry(hmawait, w->tasks.current, virtual));   // Add to HashMap

    // Set the task groups
    int wait_amount = 0;
    for (size_t i = 0; i < num_task; i++)
    {
        ASSERT_VALID_HANDLE(w, handles[i]);
        if (STATUS_OF(handles[i]) == FINISHED || handles[i] == HANDLE_IGNORE) {
            handles[i] = -1;
            continue;
        }if (w->tasks.arr[handles[i]]->detached) {
            asc_error(WARN_AWAITING_DETACHED_TASK, "__task_await_all_impl()", (any_t)handles[i]);
            handles[i] = -1;
            continue;
        }
        w->tasks.arr[handles[i]]->await_group = virtual;
        wait_amount++;
    }
    
    // The otherwise unused pointers in the TCB arr or gonna serve as remaining counters!!!
    w->tasks.arr[virtual] = (TCB*)(uintptr_t)wait_amount;
    //Yield
    _asm_ctx_switch_64(&w->sched_ctx, &w->tasks.arr[w->tasks.current]->ctx);

    // Create the result
    all_result res = { 0 };
    res.amount = wait_amount;
    res.returns = malloc(sizeof(void*) * num_task);

    // Destroy and get the results
    for (size_t i = 0; i < num_task; i++) {
        if (handles[i] == HANDLE_IGNORE) continue;
        
        // Safety check: only destroy if it hasn't been destroyed by a detach
        if (TCB_AT(handles[i])) { 
            res.returns[i] = w->tasks.arr[handles[i]]->returned;
            __task_destroy(handles[i]);
        }
    }

    w->tasks.arr[w->tasks.current]->w_reason = WAIT_NONE;
    w->tasks.arr[w->tasks.current]->wait_for.task = 0;
    
    // Remove our waiting from the hashmap
    assert(hm_remove_entry(hmawait, w->tasks.current));
    
    __task_destroy(virtual);

    // Allow the methods
    res.none = __all_result_none_method;
    res.get = __all_result_get_method;

    if (last_all_result) { free(last_all_result->returns); free(last_all_result); }
    last_all_result = malloc(sizeof(all_result));
    memcpy(last_all_result, &res, sizeof(all_result));

    return res;
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

void task_detach() {
    worker_t* w = tl_worker;
    ASSERT_CONTEXT(w, "detach()");
    w->tasks.arr[w->tasks.current]->detached = 1;
    size_t curr;

    // Alert any task that he have detached
    if (!hmawait) return;    
    for (ssize_t i = hmawait->highest_index; i >= 0; i--)
    {
        if (!hmawait->occ[i]) continue;
        curr = hmawait->map[i].k;
        if (hmawait->map[i].v == w->tasks.arr[w->tasks.current]->await_group
            && STATUS_OF(curr) == WAITING && _get_tcbstate_at(curr)) {
            // if its dependency is "virtual joined" we only decrement the number
            if (IS_JOINED(curr)) {
                uintptr_t old = (uintptr_t)w->tasks.arr[curr];
                old--;
                w->tasks.arr[curr] = (TCB*)old;
                continue;
            }
            // Otherwise simply ready the waiter
            STATUS_OF(curr) = READY;
        }
    }
    
}

void* __task_alloc_ret_val(size_t s, void* src) {
    void* dst = malloc(s);
    assert(dst);
    if (!dst) return NULL;
    memcpy(dst, src, s);
    return dst;
}

void __task_drop_impl(task_handle_t th) {
    worker_t* w = tl_worker;
    ASSERT_CONTEXT(w, "__task_drop_impl()");

    // Self drop call
    if (th == (size_t)-1) { th = w->tasks.current;}
    // Finish if invalid;
    if (!TCB_AT(th) || STATUS_OF(th) == FINISHED) {
        return;
    }

    assert(!IS_VIRTUAL(th));

    // Eliminate from rotation (important because of recursion).
    w->tasks.arr[th]->status = FINISHED;

    TCB* t = w->tasks.arr[th];

    // Unlink dependencies

    switch (t->w_reason)
    {
    case WAIT_TASK:
        __task_drop_impl(t->wait_for.task);
        break;
    case WAIT_TIMER:
        t->wait_for.timestamp = 0;
        break;
    case WAIT_FD:
        epoll_ctl(w->sync.epoll_fd, EPOLL_CTL_DEL, t->wait_for.fd, NULL);
        release_fd(t->wait_for.fd);
        break;
    
    default:
        break;
    }

    // Ready up any that might be waiting for the dropped task.
    // Reverse search, more efficient since awaiting tends to follow a stack, although not exactly a stack.
    for (ssize_t i = hmawait->highest_index; i >= 0; i--)
    {
        if (!hmawait->occ[i]) continue;
        if (hmawait->map[i].v == th && STATUS_OF(hmawait->map[i].k) != FINISHED) {
            STATUS_OF(hmawait->map[i].k) = READY;
        }
    }

    if (th < w->tasks.initial) { 
        // Signal that one task has finished
        pthread_mutex_lock(&w->sync.lock);
        w->sync.initial_remaining--;
        w->tasks.running--;
        if (w->sync.initial_remaining == 0) {
            w->sync.initial_done = 1;
            pthread_cond_broadcast(&w->sync.cond);
        }
        pthread_mutex_unlock(&w->sync.lock);
    }

    if (th != w->tasks.current) {
        __task_destroy(th);
    }
    
    //Dispatcher will clean up
    __task_yield_impl();
}


static void* __any_result_select_method() {
    if (!last_any_result) return NULL;
    void* ret = NULL;

    for (size_t i = 0; i < last_any_result->num_losers; i++)
    {
        __task_drop_impl(last_any_result->losers[i]);
    }

    free(last_any_result->losers);
    if (last_any_result->winner_return) {
        ret = *(void**)last_any_result->winner_return;
        free(last_any_result->winner_return);
    }
    free(last_any_result);
    last_any_result = NULL;

    return ret;

}

static void* __all_result_get_method(int n) {
    if (!last_all_result) return NULL;
    void* ret = NULL;

    if (last_all_result->returns[n]) {
        ret = *(void**)last_all_result->returns[n];
        free(last_all_result->returns[n]);
    }
    free(last_all_result->returns);
    free(last_all_result);
    last_all_result = NULL;
    return ret;
}
static void __all_result_none_method(){
    if (!last_all_result) return;
    free(last_all_result->returns);
    free(last_all_result);
    last_all_result = NULL;
}

void __hm_await_free() {
    hm_dealloc(&hmawait);
}
