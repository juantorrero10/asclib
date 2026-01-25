#ifndef WORKER_H_
#define WORKER_H_

#include <stdlib.h>
#include <stdint.h>



typedef struct _Task_Ctx_Block worker_t;
typedef void* task_result;
typedef size_t task_handle;

typedef enum {
    UNITIALIZED = 0,
    RUNNING,
    READY,
    WAITING,
    FINISHED,
} task_status;


typedef struct _Task_Entry_ {
    void* (*fptr)(void*);
    size_t arg_size;
    int64_t passing_value;
} task__arg__constructor;

/** 
 * @brief Macro for task creation.
 * 
 * Arg passed is an 8-byte array of content
 * Can be a pointer or a value of any format.
 * 
 * Union eliminates UB ensuring correct aligment 
 * and zeroing-out unused bytes for smaller formats like int, short, char
*/
#define task_entry(task_function, type, value)                     \
({                                                                 \
    union { int64_t i64; type v; } _u = { .i64 = 0 };              \
    _u.v = (value);                                                \
    (task__arg__constructor){                                      \
        .fptr = task_function,                                     \
        .arg_size = sizeof(type),                                  \
        .passing_value = _u.i64                                    \
    };                                                             \
})

// TASK CREATION AND CONTEOL
#define TASK(name) void* name(void* __arg)
#define task_arg(type) *((type*)&__arg)

void task_yield();
void* __task_alloc_ret_val(size_t s, void* v);

#define task_return(var, type) \
    do {\
        return __task_alloc_ret_val(sizeof(type), (void*)var);\
    } while(0);\

#define task_return_void() return NULL;
#define task_recover_from_worker(result_arr, index, type) *((type*)result_arr[index])

task_status task_get_status(task_handle th);
void* __task_await_impl(task_handle th, size_t return_size);

#define await(handle, return_type) *((return_type*)&__task_await_impl(handle))


// WORKER FUNCTION
worker_t* worker_create(size_t n_tasks, ...);
void worker_destroy(worker_t* worker);
void worker_run(worker_t* w);
task_result* worker_await_all(worker_t* w);


#endif // WORKER_H_