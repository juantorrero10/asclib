#ifndef CONTEXT_H_
#define CONTEXT_H_

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
    int64_t arg;            // Always 8-bytes
    task_status status;
    size_t waiting_for;
}TCB;

typedef struct _Thread_Worker {
    pthread_t th;
    TCB* tasks;
    size_t  n_tasks;
    size_t  current;

    ctx_t sched_ctx;
    void** returned_array;
} worker_t;

typedef struct _Task_Entry_ {
    void* (*fptr)(void*);
    size_t arg_size;
    int64_t value;
} task_entry_t;

worker_t* worker_create(size_t n_tasks, ...);
void worker_destroy(worker_t* task);

#endif // CONTEXT_H_