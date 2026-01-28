#ifndef TCB_H_
#define TCB_H_


#include "misc.h"

typedef struct _Task_Ctx_Block TCB;

typedef void* task_result;

typedef enum {
    UNITIALIZED = 0,
    RUNNING,
    READY,
    WAITING,
    FINISHED,
} task_status;

typedef enum {
    WAIT_NONE,
    WAIT_TASK,
    WAIT_FD,
    WAIT_TIMER
} wait_reason_t;


typedef struct _Task_Ctx_Block {
    void* (*fptr)(void* arg);
    void* arg;            // Always 8-bytes
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
    int dyn_created;        // 
    size_t await_group;     //
    int detached;           // Bool, A children task can outlive its father, it will clean up itself after
    void* returned;         // Singular retuned value. Outside of normal result_array

}TCB;

typedef struct size_t* await_group;

#endif // TCB_H_