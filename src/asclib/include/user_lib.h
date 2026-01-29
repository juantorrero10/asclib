#ifndef WORKER_H_
#define WORKER_H_

#include <stdlib.h>
#include <stdint.h>



typedef struct _Task_Ctx_Block worker_t;
typedef void* task_result_t;
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
        .n_pages = 2                                               \
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

#define TASK(name) void* name(void* __arg)
#define task_arg(type) *((type*)&__arg)
#define ignore_arg() (void)__arg

#define task_return(var, type) \
    do { \
        type __temp = var; \
        return __task_alloc_ret_val(sizeof(type), &__temp); \
    } while(0)

#define task_return_void() return NULL;
#define task_recover_from_worker(result_arr, index, type) *((type*)result_arr[index])

#define await(type, id) ({ \
    void* __ptr = __task_await_impl(id); \
    type __val = *(type*)__ptr; \
    free(__ptr); \
    __val; \
})

#define await_void(id) do { \
    void* __ptr = __task_await_impl(id); \
    free(__ptr); \
} while(0)

#define task_new(fptr, arg_type, value) ({\
    task_handle __val = __task_create(task_entry(fptr, arg_type, value)); \
    __val; \
})

#define task_new_ex(fptr, arg_type, value, n_pages) ({\
    task_handle __val = __task_create(task_entry_ex(fptr, arg_type, value, n_pages)); \
    __val; \
})



// WORKER FUNCTION
worker_t* worker_create(size_t running, ...);
void worker_destroy(worker_t* worker);
void worker_run(worker_t* w);
task_result_t* worker_await_all(worker_t* w);
task_result_t* worker_await(worker_t* w);


#endif // WORKER_H_