#ifndef TASKS_TASK_CONTROL_H_
#define TASKS_TASK_CONTROL_H_

void task_yield();

__attribute__((noreturn))
void task_trampoline(void);

typedef size_t task_handle_t;

#define STATUS_OF(id) tl_worker->tasks[id]->status
#define WAITING_FOR(id) tl_worker->tasks[id]->waiting_for

void* __task_alloc_ret_val(size_t s, void* v);

#endif // TASKS_TASK_CONTROL_H_