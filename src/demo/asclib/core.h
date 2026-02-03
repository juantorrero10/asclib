#ifndef ASCLIB_CORE_H_
#define ASCLIB_CORE_H_

#include <stdlib.h>
#include <stdint.h>

/* #define __ASCLIB_COUNT_ARGS(...) __ASCLIB_COUNT_ARGS_IMPL(__VA_ARGS__, 8, 7, 6, 5, 4, 3, 2, 1, 0)
#define __ASCLIB_COUNT_ARGS_IMPL(_1, _2, _3, _4, _5, _6, _7, _8, N, ...) N */

typedef struct _Task_Ctx_Block worker_t;
typedef void* task_result_t;
typedef struct _AL_TH_ { size_t val; } task_handle_t;

struct _ASCLIB_Opaque_ { void* __reserved; }; 


typedef struct _Task_Entry_ {
    void* (*fptr)(void*);
    size_t arg_size;
    void* value;
    size_t n_pages;
    int arg_malloced;
} task__arg__constructor;

typedef struct _Await_Any_Result {
    void* winner_return;
    task_handle_t winner;
    size_t winner_idx;
    task_handle_t* losers;
    size_t num_losers;
    int some;

    /** @attention Unsafe method!!!!.
     * This methods is only guaranteed to work if called inmidiately after await_any()
     * It drops the loser task and get the winners result 
     * Dropping implies that alloced heap memory gets leaked if its not freed before dropping.
     */ 
    void* (*select)(void);
    // Same as select but we keep the any_result
    struct _Await_Any_Result (*cut_losers)(void);
} any_result;

typedef struct _Await_All_Result {
    void** returns;
    size_t amount;
    void (*none)(void);
    void* (*get)(size_t);
} all_result;

typedef struct _CleanUP_Routine {
    void* __ctx;
    void (*cleanup)(void*);
} cleanup_routine_t;

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
#define task_entry(task_function, ...)                              \
({                                                                  \
    __ASCLIB__##task_function##__ARGDECL __packet =                 \
                    (__ASCLIB__##task_function##__ARGDECL){         \
        ._d = 0,                                                    \
        __VA_ARGS__                                                 \
    };                                                              \
                                                                    \
                                                                    \
    (task__arg__constructor){                                       \
        .fptr = task_function,                                      \
        .value = &__packet,                                         \
        .arg_size = sizeof(__ASCLIB__##task_function##__ARGDECL),   \
        .n_pages = TASK_DEFAULT_PAGES                               \
    };                                                              \
})

#define task_entry_ex(task_function, stack_pages, ...)              \
({                                                                  \
    __ASCLIB__##task_function##__ARGDECL __packet =                 \
                    (__ASCLIB__##task_function##__ARGDECL){         \
        ._d = 0,                                                    \
        __VA_ARGS__                                                 \
    };                                                              \
                                                                    \
                                                                    \
    (task__arg__constructor){                                       \
        .fptr = task_function,                                      \
        .value = &__packet,                                         \
        .arg_size = sizeof(__ASCLIB__##task_function##__ARGDECL),   \
        .n_pages = stack_pages                                      \
    };                                                              \
})

// Recover the return value of one of the initial tasks
#define task_recover_from_worker(result_arr, index, type) *((type*)result_arr[index])

// TASK CREATION AND CONTEOL


void __task_yield_impl();
void task_detach(task_handle_t t);
void* __task_alloc_ret_val(size_t s, void* src);
void* __task_await_impl(task_handle_t th, int expect);
any_result __task_await_any_impl(task_handle_t* handles, size_t count);
all_result __task_await_all_impl(task_handle_t* handles, size_t num_task);
task_handle_t __task_create(task__arg__constructor te);
void __task_drop_impl(task_handle_t th);


// =========================== MACROS/ERGONOMICS =============================00

// === HELPERS ====
#define __ASCLIB_COUNT_ARGS(...) (sizeof((task_handle_t []){__VA_ARGS__}) / sizeof(task_handle_t ))

// For Header files (.h). Prototype function declaration
#define decl_async(return_type, name, ...)                                \
    typedef struct { size_t _d; __VA_ARGS__; } __ASCLIB__##name##__ARGDECL; \
    void* name(void* __arg)

// For source files (.c). Body function declaration
#define async(name, ...)                                \
    typedef struct { size_t _d; __VA_ARGS__; } __ASCLIB__##name##__ARGDECL; \
    void* name(void* __arg) {                           \
        __ASCLIB__##name##__ARGDECL* __args = (__ASCLIB__##name##__ARGDECL*)__arg;        \
        (void)__args;

// Async declaration compatible for "redefinitions"
#define async_(name, ...)                                \
    void* name(void* __arg) {                           \
        __ASCLIB__##name##__ARGDECL* __args = (__ASCLIB__##name##__ARGDECL*)__arg;        \
        (void)__args;

    
#define arg(name) (__args->name)


// Shut up the compiler
#define ignore_arg() (void)__arg



/**
 * @brief Return a value from a task.
 */
#define end_return(type, var) \
    do { \
        type __temp = var; \
        return __task_alloc_ret_val(sizeof(type), &__temp); \
    } while(0); }

// Return nothing
#define end_return_void() return NULL; }

/**
 * @brief Return a value from a task.
 */
#define task_return(type, var) \
    do { \
        type __temp = var; \
        return __task_alloc_ret_val(sizeof(type), &__temp); \
    } while(0);

// Return nothing
#define task_return_void() return NULL;



// await() but expect and recover a return value.
// Runtime will panic if we expect value on a function that returns void
#define await_ret(type, handle) ({ \
    void* __ptr = __task_await_impl(handle, 1); \
    type __val = *(type*)__ptr; \
    free(__ptr); \
    __val; \
})

/**
 * @brief Awaiting something means blocking progress on the current until another finishes.
 * @note await() macro doesn't expect a return value, await_ret() does, use it instead.
 */
#define await(handle) do { \
    void* __ptr = __task_await_impl(handle, 0); \
    free(__ptr); \
} while(0)

/**
 * @brief   Block current task until of the task finishes.
 *          The one to finish first is considered the 'Winner'
 *          and the rest the 'Losers'
 * @returns Return value of the 'winner'
 * @attention 'Loser' task gets detached.
 * @attention This macro only works up to 8 tasks, 
 *             if you want more you need to manually call `__task_await_impl();`
 */
#define await_any(...) ({ \
    task_handle_t _group[] = { __VA_ARGS__ }; \
    __task_await_any_impl(_group, __ASCLIB_COUNT_ARGS(__VA_ARGS__)); \
})

#define await_all(...) ({ \
    task_handle_t _group[] = { __VA_ARGS__ }; \
    __task_await_all_impl(_group, __ASCLIB_COUNT_ARGS(__VA_ARGS__)); \
})

#define any_unwrap_or(type, res, fallback)        \
    ({                                            \
        any_result _r = (res);                \
        if (!_r.some) {                           \
            fallback;                      \
        }                                         \
        type _val = *(type*)_r.winner_return;     \
        free(_r.winner_return);                   \
        _val;                                     \
    })


#define any_ret(type, result) ({\
    void* __ptr = result.winner_return;\
    type __val = *(type*)__ptr; free(__ptr); \
    __val; \
})

/**
 * @brief Spawn a task using its name and argument values.
 * Uses designated initializers to ensure type safety.
 * Usage: task_spawn(my_func, .x = 10, .y = "hello");
 */
#define task_spawn(name, ...) ({ \
    __ASCLIB__##name##__ARGDECL* _a = malloc(sizeof(__ASCLIB__##name##__ARGDECL)); \
    *_a = (__ASCLIB__##name##__ARGDECL){ __VA_ARGS__ }; \
    task__arg__constructor _te = { .fptr = name, .value = _a, .arg_size = sizeof(__ASCLIB__##name##__ARGDECL), .n_pages = TASK_DEFAULT_PAGES , .arg_malloced = 1}; \
    __task_create(_te); \
})

/**
 * @brief Spawn a task using its name and argument values.
 * Uses designated initializers to ensure type safety.
 * Usage: task_spawn(my_func, .x = 10, .y = "hello");
 */
#define task_spawn_ex(name, stack_pages, ...) ({ \
    __ASCLIB__##name##__ARGDECL* _a = malloc(sizeof(__ASCLIB__##name##__ARGDECL)); \
    *_a = (__ASCLIB__##name##__ARGDECL){ __VA_ARGS__ }; \
    task__arg__constructor _te = { .fptr = name, .value = _a, .arg_size = sizeof(__ASCLIB__##name##__ARGDECL), .n_pages = stack_pages , .arg_malloced = 1}; \
    __task_create(_te); \
})

/**
 * @brief Yielding is voluntarialy giving control of the current thread back to the dispatcher.
 * The fundamental idea behind async execution. May not be used directly but almost every other actions yields at some point
 */
#define yield do { __task_yield_impl(); } while(0)

/**
 * @brief When a task 'detaches' its signals 
 * back to all its waiters to not wait for them.
 * Detaching also makes a task cleaup itself and guarantes to the runtime that it would not leak.
 * Return value of detached tasks get lost. 
 */
#define detach do { task_detach((task_handle_t){ -1 }); } while(0);


// WORKER FUNCTION
worker_t* worker_create(size_t running, ...);
void worker_destroy(worker_t* worker);
void worker_run(worker_t* w);
task_result_t* worker_await_all(worker_t* w);
task_result_t* worker_await(worker_t* w);


#endif // WORKER_H_