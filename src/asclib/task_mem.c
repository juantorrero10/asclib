#include <assert.h>
#include <string.h>
#include <sys/mman.h>
#include <stdio.h>

#include <task_control.h>
#include <task_stack.h>
#include <defs.h>
#include <dispatcher.h>



// Memory managment unit for TCB types


// Occupation map to prevent segmentation
TLS uint8_t* occupation_map = NULL;




// Private more priviledge task creator.
task_handle_t __task_create_ex(task_entry_t te, size_t ov_stack_size, uint32_t __create_guard_page) {

    if (__create_guard_page > 1) __create_guard_page = 1;

    // If we decide to not override
    if ( ov_stack_size == 0 ) ov_stack_size = te.n_pages * PAGE_SIZE;
    
    worker_t* w = tl_worker;
    assert(w);

    if (!occupation_map) {
        occupation_map = calloc(1, w->capacity);
        for (size_t i = 0; i < w->initial_tasks; i++)
        {
            occupation_map[i] = 1;
        }
        
    }

    TCB t = {
        .fptr = te.fptr,
        .arg = te.value,
        .status = UNITIALIZED,
        .stack_size = ov_stack_size,
        .wake_at = 0,
        .ctx = { 0 },
        .dyn_created = 1,
        .detached = 0,
        .guard_page = __create_guard_page,
        .waiting_for = (size_t)-1,
    };



    // if we dont find a free spot realloc the array;
    int new_id = -1;
    for (int i = 0; i < (int)w->capacity; i++)
        if (occupation_map[i] == 0) { new_id = i; break; }

    if ((w->n_tasks >= w->capacity) && new_id == -1) {
        size_t old_c = w->capacity;
        w->tasks = realloc(w->tasks, sizeof(TCB*) * w->capacity * 2);
        occupation_map = realloc(occupation_map, w->capacity * 2);
        w->capacity *= 2;

        // Memset to 0, destructors rely on NULL values!!
        memset(occupation_map + w->n_tasks, 0, w->capacity - w->n_tasks);
        memset(w->tasks + old_c, 0, sizeof(TCB*) * (w->capacity - old_c));
        new_id = w->n_tasks;
    }
    w->tasks[new_id] = malloc(sizeof(TCB));
    memcpy(w->tasks[new_id], &t, sizeof(TCB));

    occupation_map[new_id] = 1;

    w->n_tasks++;

    return new_id;
}

// Exposed function for task creation, its a wrapper for the more priviledge function.
// Users cannot create tasks without a guard page, insider runtime 'async' functions can and will for performance.
// For the same reason they cannot alloc by indivual bytes, only whole pages.
task_handle_t __task_create(task_entry_t te) {  return __task_create_ex(te, 0, 1); }

void __task_destroy(task_handle_t t) {
    worker_t* w = tl_worker;
    assert(w);


    dealloc_stack(w->tasks[t]);
    
    w->tasks[t]->stack = NULL;
    occupation_map[t] = 0; // Free the slot for reuse

    free(w->tasks[t]);

    w->tasks[t] = NULL;

    w->n_tasks--;
}

uint8_t _get_occupation_at(size_t id) { return occupation_map[id]; }

void _set_occupation_at(size_t id, uint8_t val) { occupation_map[id] = val; }

void __alloc_occupation_map() {

    worker_t* w = tl_worker;
    assert(tl_worker);

    if (!occupation_map) {
        occupation_map = calloc(1, w->capacity);
        for (size_t i = 0; i < w->initial_tasks; i++)
        {
            occupation_map[i] = 1;
        }
        
    }
 }

void __free_occupation_map() { 
    free(occupation_map);
    occupation_map = NULL;
}