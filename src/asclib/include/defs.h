#ifndef DEFS_H_
#define DEFS_H_

#include <stdlib.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/epoll.h>

#define MAX_EVENTS 32

#define ASSERT_SYSCALL(worker, cond, name) do {\
    if (cond) {\
        perror(name);\
        free(worker);\
        return NULL;\
    }\
} while (0)

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
#define SLEEPING(candidate) (candidate->status == WAITING && candidate->w_reason == WAIT_TIMER && candidate->wait_for.timestamp > 0)

typedef struct {
    uint8_t* rsp;
    uint8_t* rbp;
    uint8_t* rbx;
    uint8_t* r12;
    uint8_t* r13;
    uint8_t* r14;
    uint8_t* r15;
} ctx_t;

typedef enum {
    WAIT_NONE,
    WAIT_TASK,
    WAIT_FD,
    WAIT_TIMER
} wait_reason_t;

typedef struct _Task_Ctx_Block {
    void* (*fptr)(void* arg);
    int64_t arg;            // Always 8-bytes
    ctx_t ctx;

    // Stack data
    uint8_t* stack;
    size_t stack_size;
    int guard_page;            // Bool

    // Dispatcher data
    task_status status;
    wait_reason_t w_reason;
    union
    {
        size_t task;
        int64_t fd;
        uint64_t timestamp;
    } wait_for;
    

    // For non inital tasks ('dynamic tasks')
    int dyn_created;        // Bool
    int detached;           // Bool, A children task can outlive its father, it will clean up itself after
    void* returned;         // Singular retuned value. Outside of normal result_array

}TCB;

// For preventing simultaneaous from the same FD
typedef struct {
    int k_fd;         
    size_t v_task;
} fd_hash_entry;

typedef struct {
    fd_hash_entry* map;
    size_t size;
    size_t capacity;
    uint8_t* occ;
} fd_hash_map;

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

worker_t* worker_create(size_t running, ...);
void worker_destroy(worker_t* task);

#endif // DEFS_H_