#ifndef WORKER_H_
#define WORKER_H_

#include "misc.h"
#include "TCB.h"

#define MAX_EVENTS 32


typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t  cond;
    size_t          initial_remaining; 
    int             initial_done;
    int             all_done;   // All tasks finished. Detached or none.
    int             epoll_fd;
    struct epoll_event ready_events[MAX_EVENTS];
} worker_sync_t;

typedef struct {
    TCB** arr;
    size_t  capacity;   // Allocated TCB's
    size_t  running;    // Not finished tasks
    size_t  current;    // Currently focused task
    size_t  initial; // Task at worker's creation
} worker_tasks_t;

typedef struct _Thread_Worker {
    pthread_t th;           // Resposible thread
    ctx_t sched_ctx;        // register context block of dipatcher

    worker_sync_t sync;
    worker_tasks_t tasks;

    void* emergency_stack;  // to print SIGSEGV's
    void** returned_array;  // results of the inital tasks

} worker_t;

worker_t* worker_create(size_t running, ...);
void worker_destroy(worker_t* task);


#endif // WORKER_H_