#include <stdarg.h>
#include <assert.h>
#include <stdio.h>

#include <defs.h>
#include <dispatcher.h>




worker_t* worker_create(size_t n_tasks, ...) {
    va_list args;

    assert(n_tasks > 0);

    worker_t* ret = malloc(sizeof(worker_t));
    TCB* tasklisk = (TCB*)malloc(sizeof(TCB) * n_tasks);

    va_start(args, n_tasks);

    for (size_t i = 0; i < n_tasks; i++)
    {
        task_entry_t te = va_arg(args, task_entry_t);
        assert(te.fptr != NULL);
        tasklisk[i] = (TCB){ 0 };
        //Minimally initialized
        tasklisk[i] = (TCB){ 
            .fptr = te.fptr, 
            .arg = te.value, 
            .status = UNITIALIZED, 
            .ctx = { 0 },
            .waiting_for = (size_t)-1 //No one
        };
    }

    ret->current = n_tasks - 1;
    ret->n_tasks = n_tasks;
    ret->tasks = tasklisk;
    ret->th = 0;

    return ret;
}

void worker_destroy(worker_t* w) {
    for (size_t i = 0; i < w->n_tasks; i++)
    {
        free(w->returned_array[i]);
        free(w->tasks[i].stack);
    }
    free(w->returned_array);
    free(w->tasks);
    free(w);
}

void worker_run(worker_t* w) {
    tl_worker = w;

    if (!w->n_tasks || w->tasks == NULL) return;
    
    //Spawn a thread
    pthread_create(&w->th, NULL, dispatcher, (void*)w);
}

task_result* worker_await_all(worker_t* w) 
{
    pthread_join(w->th, NULL);

    return w->returned_array;
}


