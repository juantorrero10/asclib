#include <stdarg.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <defs.h>
#include <dispatcher.h>
#include <task_mem.h>
#include <log.h>
#include <ascerr.h>


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
        //Minimally initialized
        TCB template = (TCB){ 
            .fptr = te.fptr, 
            .arg = te.value, 
            .status = UNITIALIZED, 
            .dyn_created = 0,
            .ctx = { 0 },
            .wake_at = 0,
            .waiting_for = (size_t)-1 //No one
        };

        memcpy(tasklisk[i], &template, sizeof(TCB));
    }

    pthread_mutex_init(&ret->sync_lock, NULL);
    pthread_cond_init(&ret->sync_cond, NULL);
    ret->initial_remaining = n_tasks; // Count down from here
    ret->initial_done = 0;
    ret->current = n_tasks - 1; // so that RR will rollback to 0 in first round 
    ret->n_tasks = ret->initial_tasks = n_tasks;
    ret->capacity = ret->n_tasks;
    ret->tasks = tasklisk;
    ret->th = 0;

    return ret;
}

void worker_destroy(worker_t* w) {
    if (!w->all_finished && w->initial_done) {
        asc_error(ERR_TASK_OUTLIVED_DESTROYER, "worker_destroy()", 0); 
    } else if (!w->initial_done){
        asc_error(PANICK_UNAWAITED_DESTROY, "worker_destroy()", 0);
    }

    for (size_t i = 0; i < w->n_tasks; i++)
    {
        if (i < w->initial_tasks) {
            INFO("returned array: %p\n", w->returned_array);
            if (w->returned_array[i] != NULL) 
                free(w->returned_array[i]);
            free(w->tasks[i]->stack);
        }
        free(w->tasks[i]);
    }

    free(w->returned_array);
    free(w->tasks);
    free(w);
}

void worker_run(worker_t* w) {

    if (!w) return;
    if (!w->n_tasks || w->tasks == NULL) return;

    pthread_mutex_lock(&w->sync_lock);
    w->initial_done = 0;
    pthread_mutex_unlock(&w->sync_lock);
    
    //Spawn a thread
    pthread_create(&w->th, NULL, dispatcher, (void*)w);
}


task_result* worker_await_all(worker_t* w) 
{
    if (!w) return NULL;
    pthread_join(w->th, NULL);
    w->all_finished = 1;
    return w->returned_array;
}

task_result* worker_await(worker_t* w) {

    if (!w) return NULL;
    pthread_mutex_lock(&w->sync_lock);
    while(!w->initial_done) pthread_cond_wait(&w->sync_cond, &w->sync_lock);
    pthread_mutex_unlock(&w->sync_lock);
    
    // We can return the results of the original tasks.
    // The detached tasks's retuned values get lost anyway.
    return w->returned_array;
}


