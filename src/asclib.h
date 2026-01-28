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
    size_t n_pages;
} task__arg__constructor;

#define TASK_DEFAULT_PAGES 2

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
        .passing_value = _u.i64,                                   \
        .n_pages = TASK_DEFAULT_PAGES                              \
    };                                                             \
})

#define task_entry_ex(task_function, type, value, n_stack_pages)   \
({                                                                 \
    union { int64_t i64; type v; } _u = { .i64 = 0 };              \
    _u.v = (value);                                                \
    (task__arg__constructor){                                      \
        .fptr = task_function,                                     \
        .arg_size = sizeof(type),                                  \
        .passing_value = _u.i64,                                   \
        .n_pages = n_stack_pages                                   \
    };                                                             \
})

// TASK CREATION AND CONTEOL


void __task_yield_impl();
void task_detach();
void* __task_alloc_ret_val(size_t s, void* src);
task_status task_get_status(task_handle th);
void* __task_await_impl(task_handle th);
task_handle __task_create(task__arg__constructor te);

// Task can only have one funtion signature.
#define async(name) void* name(void* __arg)

// Recover the argument
#define task_arg(type) *((type*)&__arg)

// Shut up the compiler
#define ignore_arg() (void)__arg

// Give control back to dispatcher.
#define yield do { __task_yield_impl(); } while(0)

#define detach do { task_detach(); } while(0);

/**
 * @brief Return a value from a task.
 */
#define task_return(var, type) \
    do { \
        type __temp = var; \
        return __task_alloc_ret_val(sizeof(type), &__temp); \
    } while(0)

// Return nothing
#define task_return_void() return NULL;

// Recover the return value of one of the initial tasks
#define task_recover_from_worker(result_arr, index, type) *((type*)result_arr[index])

// await() but expect and recover a return value.
#define await_ret(type, handle) ({ \
    void* __ptr = __task_await_impl(handle); \
    type __val = *(type*)__ptr; \
    free(__ptr); \
    __val; \
})

// Block the current task until 
#define await(handle) do { \
    void* __ptr = __task_await_impl(handle); \
    free(__ptr); \
} while(0)

// Task constructor to create the worker, stack defaults to 8Kb.
#define task_new(fptr, arg_type, value) ({\
    task_handle __val = __task_create(task_entry(fptr, arg_type, value)); \
    __val; \
})

/**
 * @brief Extended task_new macro
 * @param n_pages N of memory pages (normally 4Kb) allocated as a stack.
 */
#define task_new_ex(fptr, arg_type, value, n_pages) ({\
    task_handle __val = __task_create(task_entry_ex(fptr, arg_type, value, n_pages)); \
    __val; \
})



// WORKER FUNCTION
worker_t* worker_create(size_t running, ...);
void worker_destroy(worker_t* worker);
void worker_run(worker_t* w);
task_result* worker_await_all(worker_t* w);
task_result* worker_await(worker_t* w);


#endif // WORKER_H_