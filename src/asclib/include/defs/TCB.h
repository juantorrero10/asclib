#ifndef TCB_H_
#define TCB_H_


#include "misc.h"

typedef struct _Task_Ctx_Block TCB;

typedef struct _Generic_Array_List g_arrlist_t;

typedef void* task_result_t;

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
    WAIT_CHANNEL,
    WAIT_TIMER
} wait_reason_t;




typedef struct _Task_Ctx_Block {
    void* (*fptr)(void* arg);
    void* arg;            // Always 8-bytes
    int arg_is_malloced;
    ctx_t ctx;

    //Clean up
    g_arrlist_t* cleanup;
    void* tls;

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

#endif // TCB_H_