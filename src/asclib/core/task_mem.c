#include <assert.h>
#include <string.h>
#include <sys/mman.h>
#include <stdio.h>


#include <task_control.h>
#include <task_stack.h>
#include <defs.h>
#include <dispatcher.h>
#include <log.h>



// Memory managment unit for TCB types


// Occupation map to prevent segmentation
TLS uint8_t* occupation_map = NULL;




/** 
 * @brief Task creator. 
 * @param te IF te.fptr == NULL it will create a 'virtual task'
 * @note a virtual task doesnt allocate a TCB it just occupies a spot.
 * @attention virtual tasks will grow the array if there aren't any space left.
*/
task_handle_t __task_create_ex(task_entry_t te, size_t ov_stack_size, uint32_t __create_guard_page) {

    // Cap to 1.
    if (__create_guard_page > 1) __create_guard_page = 1;

    // If we decide to not override
    if ( ov_stack_size == 0 ) ov_stack_size = te.n_pages * PAGE_SIZE;
    
    worker_t* w = tl_worker;
    assert(w);

    int virtual_task = 0;

    if (!occupation_map) {
        occupation_map = calloc(1, w->tasks.capacity);
        for (size_t i = 0; i < w->tasks.initial; i++)
        {
            occupation_map[i] = 1;
        }
        
    }
    
    if (!te.fptr) virtual_task = 1;
    
    TCB t = {
        .fptr = te.fptr,
        .arg = te.value,
        .status = UNITIALIZED,
        .stack_size = ov_stack_size,
        .arg_is_malloced = te.arg_malloced,
        .w_reason = WAIT_NONE,
        .wait_for.fd = -1,
        .ctx = { 0 },
        .dyn_created = 1,
        .await_group = (size_t)-1,
        .detached = 0,
        .guard_page = __create_guard_page,
    };





    // if we dont find a free spot realloc the array;
    int new_id = -1;
    for (int i = 0; i < (int)w->tasks.capacity; i++)
        if (occupation_map[i] == 0) { new_id = i; break; }

    if ((w->tasks.running >= w->tasks.capacity) && new_id == -1) {

        size_t old_c = w->tasks.capacity;

        w->tasks.arr =      realloc(w->tasks.arr, sizeof(TCB*) * w->tasks.capacity * 2);
        occupation_map =    realloc(occupation_map, w->tasks.capacity * 2);

        w->tasks.capacity *= 2;

        // Memset to 0, destructors rely on NULL values!!
        memset(occupation_map + old_c, 0, w->tasks.capacity - old_c);
        memset(w->tasks.arr + old_c, 0, sizeof(TCB*) * (w->tasks.capacity - old_c));
        new_id = w->tasks.running;
        

    }

    if (!virtual_task) {
        w->tasks.arr[new_id] = malloc(sizeof(TCB));
        printf("[NEW TCB] ID %d at %p\n", new_id, (void*)w->tasks.arr[new_id]);
        memcpy(w->tasks.arr[new_id], &t, sizeof(TCB));
    } else {
        //IMPORTANT!!
        w->tasks.arr[new_id] = NULL;
    }
    
    occupation_map[new_id] = 1;

    w->tasks.running++;

    return new_id;
}

// Exposed function for task creation, its a wrapper for the more priviledge function.
// Users cannot create tasks without a guard page, insider runtime 'async' functions can and will for performance.
// For the same reason they cannot alloc by indivual bytes, only whole pages.
task_handle_t __task_create(task_entry_t te) {  return __task_create_ex(te, 0, 1); }

void __task_destroy(task_handle_t t) {
    worker_t* w = tl_worker;
    assert(w);

    TCB* ta = w->tasks.arr[t];

    if (ta != NULL) {
        if (ta->w_reason == WAIT_FD) {
            if (ta->wait_for.fd >= 0) {
                epoll_ctl(w->sync.epoll_fd, EPOLL_CTL_DEL, ta->wait_for.fd, NULL);
            }
        }
        
        // Reset to safe defaults before freeing memory
        ta->w_reason = WAIT_NONE;
        if (ta->arg_is_malloced) free(ta->arg);
        dealloc_stack(w->tasks.arr[t]);
        w->tasks.arr[t]->stack = NULL;
    }

    occupation_map[t] = 0; // Free the slot for reuse

    // If it's virtual it will see a NULL and do nothing
    free(w->tasks.arr[t]);

    w->tasks.arr[t] = NULL;

    w->tasks.running--;
}

uint8_t _get_occupation_at(size_t id) { return occupation_map[id]; }

void _set_occupation_at(size_t id, uint8_t val) { occupation_map[id] = val; }

void __alloc_occupation_map() {

    worker_t* w = tl_worker;
    assert(tl_worker);

    if (!occupation_map) {
        occupation_map = calloc(1, w->tasks.capacity);
        for (size_t i = 0; i < w->tasks.initial; i++)
        {
            occupation_map[i] = 1;
        }
        
    }
 }

void __free_occupation_map() { 
    free(occupation_map);
    occupation_map = NULL;
}