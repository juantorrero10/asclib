#ifndef MISC_H_
#define MISC_H_

#include <stdlib.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/epoll.h>

#define ASSERT_SYSCALL(worker, cond, name) do {\
    if (cond) {\
        perror(name);\
        free(worker);\
        return NULL;\
    }\
} while (0)

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

typedef struct _Task_Entry_ {
    void* (*fptr)(void*);
    size_t arg_size;
    void* value;
    size_t n_pages;
    int arg_malloced;
} task_entry_t;

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

#endif // !MISC_H_