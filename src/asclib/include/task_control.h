#ifndef TASKS_TASK_CONTROL_H_
#define TASKS_TASK_CONTROL_H_

#include <stdint.h>

void __task_yield_impl();
void __task_wait_on_fd(int fd, uint32_t events);


__attribute__((noreturn))
void task_trampoline(void);



typedef size_t task_handle_t;

#define STATUS_OF(id) tl_worker->tasks.arr[id]->status
#define IS_WAITING_FOR_US(w, id) w->tasks.arr[id]->w_reason == WAIT_TASK && w->tasks.arr[id]->wait_for.task == w->tasks.current 

#define ASSERT_CONTEXT(worker, fn) do {\
    if (!worker) asc_error(PANIC_CONTEXT_VIOLATION, fn, (any_t)0); \
} while (0)

#define ASSERT_VALID_HANDLE(worker, id) do {\
    if (id == worker->tasks.current) asc_error(PANIC_AWAIT_SELF, "await()", (any_t)id);\
    else if ( (size_t)id >= worker->tasks.capacity || !_get_occupation_at(id)) {\
        asc_error(PANIC_AWAIT_INVALID_ID, "await()", (any_t)id); \
    }\
} while (0)

#define HANDLE_IGNORE (size_t)-1

void* __task_alloc_ret_val(size_t s, void* v);


#endif // TASKS_TASK_CONTROL_H_