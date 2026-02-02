#include <assert.h>
#include <string.h>
#include <sys/mman.h>
#include <stdio.h>

#define __DEBUG

#include <ascerr.h>
#include <task_mem.h>
#include <task_control.h>
#include <task_stack.h>
#include <defs.h>
#include <dispatcher.h>
#include <log.h>
#include <collections.h>



// Memory managment unit for TCB types


// Keep memory packed and keep track of Virtual Tasks
TLS uint8_t* tcb_state_map = NULL;

// Generation map to detect stale handlers
TLS g_arrlist_t* generation_map = NULL;




/** 
 * @brief Task creator. 
 * @param te IF te.fptr == NULL it will create a 'virtual task'
 * A Virtual Task occupies a space in the TCB array but no TCB is allocated.
 * Used for making complex depency graphs during operations like await_any() or await_all()
 * @attention virtual tasks will grow the array if there aren't any space left.
*/
task_handle_t __task_create_ex(task_entry_t te, size_t ov_stack_size, uint32_t __create_guard_page) {

    // Cap to 1.
    if (__create_guard_page > 1) __create_guard_page = 1;

    // If we decide to not override
    if ( ov_stack_size == 0 ) ov_stack_size = te.n_pages * PAGE_SIZE;
    
    worker_t* w = tl_worker;
    ASSERT_CONTEXT(w, "__task_create_ex()");

    // Start generation map if not present
    if (!generation_map) {
        generation_map = gal_alloc(w->tasks.initial, sizeof(uint32_t));
        uint32_t one = 1;
        for (size_t i = 0; i < w->tasks.initial; i++)
        {
            gal_append(generation_map, &one);
        }
        
    }

    int virtual_task = 0;

    if (!tcb_state_map) {
        tcb_state_map = calloc(1, w->tasks.capacity);
        for (size_t i = 0; i < w->tasks.initial; i++)
        {
            tcb_state_map[i] = 1;
        }
        
    }
    
    if (!te.fptr) virtual_task = 1;
    
    TCB t = {
        .fptr = te.fptr,
        .arg = te.value,
        .cleanup = NULL,
        .status = UNITIALIZED,
        .stack_size = ov_stack_size,
        .arg_is_malloced = te.arg_malloced,
        .w_reason = WAIT_NONE,
        .wait_for.fd = -1,
        .ctx = { 0 },
        .tls = NULL,
        .dyn_created = 1,
        .await_group = (size_t)-1,
        .detached = 0,
        .guard_page = __create_guard_page,
    };





    // if we dont find a free spot realloc the array;
    size_t new_id = (size_t)-1;
    for (int i = 0; i < (int)w->tasks.capacity; i++)
        if (tcb_state_map[i] == 0) { new_id = (size_t)i; break; }

    if ((w->tasks.running >= w->tasks.capacity) && new_id == (size_t)-1) {

        size_t old_c = w->tasks.capacity;

        w->tasks.arr =      realloc(w->tasks.arr, sizeof(TCB*) * w->tasks.capacity * 2);
        tcb_state_map =    realloc(tcb_state_map, w->tasks.capacity * 2);

        w->tasks.capacity *= 2;

        // Memset to 0, destructors rely on NULL values!!
        memset(tcb_state_map + old_c, 0, w->tasks.capacity - old_c);
        memset(w->tasks.arr + old_c, 0, sizeof(TCB*) * (w->tasks.capacity - old_c));
        new_id = w->tasks.running;
        

    }

    uint32_t final_gen = 1;
    if (new_id == 5) {
        (void)final_gen;
    }

    if (new_id >= generation_map->size) {
        uint32_t start_gen = 1;
        gal_append(generation_map, &start_gen);
        final_gen = start_gen;
    } else {
        gal_get_at(generation_map, (int)new_id, &final_gen);
        // Only increment for real tasks being REUSED
        if (!virtual_task) {
            final_gen++;
            gal_set_at(generation_map, (int)new_id, &final_gen);
        }

    }

    if (!virtual_task) {
        w->tasks.arr[new_id] = malloc(sizeof(TCB));
        memcpy(w->tasks.arr[new_id], &t, sizeof(TCB));
    } else {
        //IMPORTANT!!
        w->tasks.arr[new_id] = NULL;
    }
    
    tcb_state_map[new_id] = 1;



    w->tasks.running++;

    // Task handle (only for the user)
    // lower 32 TCB offset
    // upper 32 generation number

    // Cast gen to 64-bit BEFORE shifting, or it will overflow before the OR
    task_handle_t final_handle = ((uint64_t)final_gen << 32) | (uint32_t)new_id;

    return final_handle;
}

// Exposed function for task creation, its a wrapper for the more priviledge function.
// Users cannot create tasks without a guard page, insider runtime 'async' functions can and will for performance.
// For the same reason they cannot alloc by indivual bytes, only whole pages.
task_handle_t __task_create(task_entry_t te) {  return __task_create_ex(te, 0, 1); }

void __task_destroy(task_handle_t t) {
    worker_t* w = tl_worker;
    assert(w);

    TCB* ta = w->tasks.arr[t];

    if (TCB_AT(t)) {
        if (ta->w_reason == WAIT_FD) {
            if (ta->wait_for.fd >= 0) {
                epoll_ctl(w->sync.epoll_fd, EPOLL_CTL_DEL, ta->wait_for.fd, NULL);
            }
        }
        
        // Reset to safe defaults before freeing memory
        ta->w_reason = WAIT_NONE;
        if (ta->cleanup) {
            // Semi-Automatic cleanup feature
            __drop_designated_objects(t, w);
            gal_dealloc(&ta->cleanup); 
        }
        if (ta->tls) {
            _env_call_destructors(ta);
            free(ta->tls); 
        }
        if (ta->arg_is_malloced) free(ta->arg);
        if (ta->stack)dealloc_stack(ta);
        ta->stack = NULL;
        free(ta);
    }


    tcb_state_map[t] = 0; // Free the slot for reuse
    w->tasks.arr[t] = NULL;

    w->tasks.running--;
}

uint8_t _get_tcbstate_at(size_t id) {
    return tcb_state_map[id & 0xFFFFFFFF]; 
}

void _set_tcbstate_at(size_t id, uint8_t val) { tcb_state_map[id] = val; }

void __alloc_occupation_map() {

    worker_t* w = tl_worker;
    assert(tl_worker);

    if (!tcb_state_map) {
        tcb_state_map = calloc(1, w->tasks.capacity);
        for (size_t i = 0; i < w->tasks.initial; i++)
        {
            tcb_state_map[i] = 1;
        }
        
    }
 }

void __free_taskmem_objs() { 
    free(tcb_state_map);
    gal_dealloc(&generation_map);
    tcb_state_map = NULL;
}