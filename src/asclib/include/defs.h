#ifndef DEFS_H_
#define DEFS_H_

#include <stdlib.h>
#include <pthread.h>
#include <stdint.h>

typedef struct _Task_Ctx_Block TCB;

typedef void* task_result;

typedef enum {
    UNITIALIZED = 0,
    RUNNING,
    READY,
    WAITING,
    FINISHED,
} task_status;


#define STATUS_RUNNABLE(s) (s == READY || s == UNITIALIZED)
#define SLEEPING(candidate) (candidate->status == WAITING && candidate->wake_at > 0)

typedef struct {
    uint8_t* rsp;
    uint8_t* rbp;
    uint8_t* rbx;
    uint8_t* r12;
    uint8_t* r13;
    uint8_t* r14;
    uint8_t* r15;
} ctx_t;

typedef struct _Task_Ctx_Block {
    void* (*fptr)(void* arg);
    ctx_t ctx;

    uint8_t* stack;
    size_t stack_size;
    int guard_page;

    int64_t arg;            // Always 8-bytes
    task_status status;
    size_t waiting_for;

    // NON BLOCKING I/O
    uint64_t wake_at;       // sleep

    // For non inital tasks
    int dyn_created;
    int detached;           // A children task can outlive its father
    void* returned;

}TCB;

typedef struct _Thread_Worker {
    pthread_t th;
    int all_finished;

    //Used for awaiting (lazy-waiting)
    pthread_mutex_t sync_lock;
    pthread_cond_t  sync_cond;
    size_t          initial_remaining; 
    int             initial_done;

    TCB** tasks;
    size_t  capacity;
    size_t  n_tasks;    // Not finished tasks
    size_t  current;

    size_t initial_tasks;

    ctx_t sched_ctx;
    void** returned_array;

    void* emergency_stack;

} worker_t;



typedef struct _Task_Entry_ {
    void* (*fptr)(void*);
    size_t arg_size;
    int64_t value;
    size_t n_pages;
} task_entry_t;

#define task_entry_ex(task_function, type, pvalue, n_stack_pages)  \
({                                                                 \
    union { int64_t i64; type v; } _u = { .i64 = 0 };              \
    _u.v = (pvalue);                                                \
    (task_entry_t){                                                \
        .fptr = task_function,                                     \
        .arg_size = sizeof(type),                                  \
        .value = _u.i64,                                           \
        .n_pages = n_stack_pages                                   \
    };                                                             \
})

worker_t* worker_create(size_t n_tasks, ...);
void worker_destroy(worker_t* task);

#endif // DEFS_H_