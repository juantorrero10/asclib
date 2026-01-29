#include <stdarg.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define __DEBUG

#include <defs.h>
#include <dispatcher.h>
#include <task_mem.h>
#include <log.h>
#include <ascerr.h>
#include <task_stack.h>


worker_t* worker_create(size_t n_tasks, ...) {
    va_list args;

    assert(n_tasks > 0);

    worker_t* ret = malloc(sizeof(worker_t));
    TCB** tasklisk = (TCB**)malloc(sizeof(TCB*) * n_tasks);

    va_start(args, n_tasks);

    for (size_t i = 0; i < n_tasks; i++)
    {
        task_entry_t te = va_arg(args, task_entry_t);
        assert(te.fptr != NULL);
        tasklisk[i] = malloc(sizeof(TCB));
        // Minimal required initalization
        TCB template = (TCB){ 
            .fptr = te.fptr, 
            .arg = te.value, 
            .status = UNITIALIZED, 
            .dyn_created = 0,
            .ctx = { 0 },
            .w_reason = WAIT_NONE,
            .wait_for.fd = -1,
            .await_group = (size_t)-1,
            .guard_page = 1,
            .stack_size = te.n_pages * PAGE_SIZE, //No one
        };

        memcpy(tasklisk[i], &template, sizeof(TCB));
    }

    //Syncing
    pthread_mutex_init(&ret->sync.lock, NULL);
    pthread_cond_init(&ret->sync.cond, NULL);
    ret->sync.initial_remaining = n_tasks; // Count down from here

    //Epoll
    ret->sync.epoll_fd = epoll_create1(0);
    ASSERT_SYSCALL(ret, ret->sync.epoll_fd == -1, "epoll_create1");
    ret->sync.initial_done = 0;
    ret->tasks.current = n_tasks - 1; // so that RR will rollback to 0 in first round 
    ret->tasks.running = ret->tasks.initial = n_tasks;
    ret->tasks.capacity = ret->tasks.running;
    ret->tasks.arr = tasklisk;
    ret->th = 0;

    return ret;
}

void worker_destroy(worker_t* w) {
    assert(w);

    if (!w->sync.all_done && w->sync.initial_done) {
        asc_error(ERR_TASK_OUTLIVED_DESTROYER, "worker_destroy()", (any_t)0); 
    } else if (w->sync.initial_remaining){
        asc_error(PANIC_UNAWAITED_DESTROY, "worker_destroy()", (any_t)0);
    }


    for (size_t i = 0; i < w->tasks.capacity; i++)
    {
        if (w->tasks.arr[i] == NULL) continue;
        if (i < w->tasks.initial) {
            if (w->returned_array[i] != NULL) {
                
                free(w->returned_array[i]);
                
            }
            dealloc_stack(w->tasks.arr[i]);
            
        }
        free(w->tasks.arr[i]->arg); 
        free(w->tasks.arr[i]);
    }
    free(w->returned_array);
    free(w->tasks.arr);
    free(w->emergency_stack);
    close(w->sync.epoll_fd);
    // OS deletion of threads is not inmediate after thread returns
    // It doesn't trigger an error because we are exiting gracefully from thread.
    // The wait time is very minimal. The OS has to do some cleanup.
    pthread_join(w->th, NULL); 
    free(w);
}

void worker_run(worker_t* w) {

    if (!w) return;
    if (!w->tasks.running || w->tasks.arr == NULL) return;

    pthread_mutex_lock(&w->sync.lock);
    w->sync.initial_done = 0;
    pthread_mutex_unlock(&w->sync.lock);
    
    //Spawn a thread
    pthread_create(&w->th, NULL, dispatcher, (void*)w);
}


task_result_t* worker_await_all(worker_t* w) 
{
    if (!w) return NULL;
    pthread_join(w->th, NULL);
    w->sync.all_done = 1;
    return w->returned_array;
}

task_result_t* worker_await(worker_t* w) {

    if (!w) return NULL;
    pthread_mutex_lock(&w->sync.lock);
    while(!w->sync.initial_done) pthread_cond_wait(&w->sync.cond, &w->sync.lock);
    pthread_mutex_unlock(&w->sync.lock);

    if (w->tasks.running == 0) {
        w->sync.all_done = 1;
    }
    
    // We can return the results of the original tasks.
    // The detached tasks's retuned values get lost anyway.
    return w->returned_array;
}


