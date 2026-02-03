#ifndef TASKS_TASK_CONTROL_H_
#define TASKS_TASK_CONTROL_H_

#include <stdint.h>

void __task_yield_impl();
void __task_wait_on_fd(int fd, uint32_t events);
void __hm_await_free();


__attribute__((noreturn))
void task_trampoline(void);





typedef size_t task_handle_t;

typedef struct _Await_Any_Result {
    void* winner_return;
    task_handle_t winner;
    size_t winner_idx;
    task_handle_t* losers;
    size_t num_losers;
    int some;
    void* (*select)(void);
    struct _Await_Any_Result (*cut_losers)(void);
} any_result;

typedef struct _Await_All_Result {
    void** returns;
    size_t amount;
    void (*none)(void);
    void* (*get)(size_t);
} all_result;

#define STATUS_OF(id) tl_worker->tasks.arr[id]->status
#define IS_WAITING_FOR(w, id, id2) w->tasks.arr[id]->w_reason == WAIT_TASK && w->tasks.arr[id]->wait_for.task == id2
#define W_REASON(id) tl_worker->tasks.arr[id]->w_reason

#define EMPTY_STATE 0
#define TCB_STATE 1
#define VIRTUAL_RACE 2
#define VIRTUAL_JOINED 3

#define NOTHING_AT(id) (_get_tcbstate_at(id) == (uint8_t)0)

#define TCB_AT(id) (_get_tcbstate_at(id) == (uint8_t)1)

#define IS_VIRTUAL(id) (_get_tcbstate_at(id) > (uint8_t)1)

#define SET_VIRTUAL_RACE(id)    (_set_tcbstate_at(id, (uint8_t)2))
#define SET_VIRTUAL_JOINED(id)  (_set_tcbstate_at(id, (uint8_t)3))
#define IS_RACE(id)             (_get_tcbstate_at(id) == (uint8_t)2)
#define IS_JOINED(id)           (_get_tcbstate_at(id) == (uint8_t)3)

#define SEPARATE_ID_GEN(handle, dst_id, dst_gen) do {\
    *dst_gen = (handle & (0xFFFFFFFFUL << 32)) >> 32;\
    *dst_id = (handle & 0xFFFFFFFF);\
} while(0)

#define ASSERT_CONTEXT(worker, fn) do {\
    if (!worker) asc_error(PANIC_CONTEXT_VIOLATION, fn, (any_t)0); \
} while (0)

#define LOWER_32(n) (n & 0xFFFFFFFF)

#define ASSERT_VALID_HANDLE(worker, id, gen_map, gen) do {\
    if (id == worker->tasks.current) asc_error(PANIC_AWAIT_SELF, "await()", (any_t)((uint64_t)gen << 32 | id));\
    else if ( (gal_cmp_at(gen_map, (int)id, &gen) != 0)) {\
        asc_error(WARN_STALE_HANDLE, "await", (any_t) ((uint64_t)gen << 32 | id));\
    } else if ((size_t)id >= worker->tasks.capacity || NOTHING_AT(id)) {\
        asc_error(PANIC_AWAIT_INVALID_ID, "await", (any_t)((uint64_t)gen << 32 | id)); \
    }\
} while (0)

#define GET_FULL_HANDLE() ({\
    uint32_t temp;\
    gal_get_at(generation_map, w->tasks.current, &temp);\
    (uint64_t)temp << 32 | w->tasks.current;\
})

#define IS_STALE(th1) ({\
    size_t index = LOWER_32(th1);\
    uint32_t buff = 0;\
    gal_get_at(generation_map, index, &buff);\
    (th1 >> 32) != buff;\
})

#define HANDLE_IGNORE (size_t)-1

void* __task_alloc_ret_val(size_t s, void* v);


#endif // TASKS_TASK_CONTROL_H_