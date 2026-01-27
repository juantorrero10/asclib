#ifndef TASKS_TASK_CONTROL_H_
#define TASKS_TASK_CONTROL_H_

void __task_yield_impl();

__attribute__((noreturn))
void task_trampoline(void);

typedef size_t task_handle_t;

#define STATUS_OF(id) tl_worker->tasks[id]->status
#define WAITING_FOR(id) tl_worker->tasks[id]->waiting_for

#define ASSERT_CONTEXT(worker, fn) do {\
    if (!worker) asc_error(PANIC_CONTEXT_VIOLATION, fn, 0); \
} while (0)

#define ASSERT_VALID_HANDLE(worker, id) do {\
    if (id == worker->current) asc_error(PANIC_AWAIT_SELF, "await()", id);\
    else if ( id >= worker->capacity || !_get_occupation_at(id)) {\
        asc_error(PANIC_AWAIT_INVALID_ID, "await()", id); \
    }\
} while (0)

#define HANDLE_IGNORE (size_t)-1

void* __task_alloc_ret_val(size_t s, void* v);

#endif // TASKS_TASK_CONTROL_H_