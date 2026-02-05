#include <string.h>
#include <stdio.h>
#include <collections.h>
#include <defs/worker.h>
#include <task_mem.h>
#include <task_control.h>

#define __DEBUG
#include <log.h>

/* ==================== HASH MAP (GENERAL) ==============================0 */
uu_hash_map* hm_alloc(size_t capacity) {
    uu_hash_map* ret = NULL;
    ret = malloc(sizeof(uu_hash_map));
    ret->map = malloc(capacity * sizeof(uu_hash_entry));
    ret->occ = malloc(capacity);
    memset(ret->occ, 0, capacity);
    ret->capacity = capacity;
    ret->size = 0;
    ret->highest_index = 0;

    return ret;
}

void hm_dealloc(uu_hash_map** hashmap) {
    uu_hash_map* hm = *hashmap;
    if (!hm) return;
    free(hm->map);
    free(hm->occ);
    free(hm);
    *hashmap = NULL;
}

int hm_add_entry(uu_hash_map* hm, size_t k, size_t v) {
    

    int first_hole = -1;

    // Search if key is present
    for (size_t i = 0; i < hm->capacity; i++)
    {
        if (hm->occ[i] == 0) { 
            if(first_hole == -1) {
                first_hole = i; 
            } 
            continue; 
        }
        if (hm->map[i].k == k) { 
            return 0; 
        }
    }

    // Expand if full
    if (first_hole == -1) {
        size_t old_cap = hm->capacity;
        hm->capacity *= 2;

        hm->map = realloc(hm->map, sizeof(uu_hash_entry) * hm->capacity);
        hm->occ = realloc(hm->occ, hm->capacity);
        memset(hm->occ + old_cap, 0, old_cap);
        first_hole = old_cap;
    }

    //Copy
    uu_hash_entry e = { .k = k, .v = v };

    memcpy(hm->map + first_hole, &e, sizeof(uu_hash_entry));
    hm->occ[first_hole] = 1;
    if ((size_t)first_hole > hm->highest_index) hm->highest_index = first_hole;
    hm->size++;
    return 1;
}

int hm_remove_entry(uu_hash_map* hm, size_t fd) {
    int found = -1;

    // Search for key
    for (size_t i = 0; i < hm->capacity; i++)
    {
        if (hm->occ[i] == 0) { continue; }
        if (hm->map[i].k == fd) { found = i; break;}
    }
    if (found == -1) return 0;
    hm->size--;
    hm->occ[found] = 0;

    if ((size_t)found >= hm->highest_index && hm->highest_index > 0) hm->highest_index = found - 1;

    return 1;
}

size_t hm_get_v(uu_hash_map* hm, size_t k) {
    for (size_t i = 0; i < hm->capacity; i++)
    {
        if (hm->occ[i] == 0) continue;
        if (hm->map[i].k == k) return hm->map[i].v;
    }
    return (size_t)-1;
}

// =========================== TIMER HEAP (specific) ==============================================0

timer_heap_t* heap_alloc(size_t initial_capacity) {
    timer_heap_t* ret = malloc(sizeof(timer_heap_t));
    ret->data = malloc(initial_capacity * sizeof(timeout_t));
    ret->capacity = initial_capacity;
    ret->size = 0;
    return ret;
}

void heap_dealloc(timer_heap_t** h) {
    free((*h)->data);
    free((*h));
    *h = NULL;
}

timeout_t heap_pop(timer_heap_t* hp) {
    timeout_t root = hp->data[0];
    timeout_t last = hp->data[--hp->size];
    
    size_t i = 0;
    while (i * 2 + 1 < hp->size) {
        size_t child = i * 2 + 1;
        // Find the smaller child
        if (child + 1 < hp->size && hp->data[child + 1].timeout < hp->data[child].timeout) {
            child++;
        }
        
        if (last.timeout <= hp->data[child].timeout) break;
        
        hp->data[i] = hp->data[child];
        i = child;
    }
    hp->data[i] = last;
    return root;
}

void heap_push(timer_heap_t* hp, size_t task_id, uint64_t timeout) {
    if (hp->size >= hp->capacity) {
        hp->capacity = (hp->capacity == 0) ? 4 : hp->capacity * 2;
        hp->data = realloc(hp->data, sizeof(timeout_t) * hp->capacity);
    }

    size_t i = hp->size++;
    while (i > 0) {
        size_t p = (i - 1) / 2;
        // Compare against the timeout stored in the heap, not the TCB
        if (hp->data[p].timeout <= timeout) break;
        
        hp->data[i] = hp->data[p];
        i = p;
    }
    hp->data[i].th = task_id;
    hp->data[i].timeout = timeout;
}

// -------------------------- u64 arrlist (general) ------------------------------------
u64_arrlist_t* u64al_alloc(size_t initial_capacity) {
    u64_arrlist_t*  ret = malloc(sizeof(u64_arrlist_t));
    ret->data = malloc(sizeof(uint64_t) * initial_capacity);
    ret->capacity = initial_capacity;
    ret->size = 0;

    return ret;
}

void u64al_dealloc(u64_arrlist_t** arr) {
    if (!(*arr)) return;
    free((*arr)->data);
    free((*arr));
    *arr = NULL;
}

void u64al_append(u64_arrlist_t* arr, uint64_t entry) {
    if (arr->size >= arr->capacity) {
        arr->data = realloc(arr->data, sizeof(uint64_t) * 2 * arr->capacity);
        arr->capacity *= 2;
    }
    arr->data[arr->size++] = entry;
}

uint64_t u64al_pop(u64_arrlist_t* arr, int _set_zero) {
    if (arr->size == 0) return (uint64_t)-1;
    uint64_t ret = arr->data[arr->size-1];
    if (_set_zero) arr->data[arr->size-1] = 0UL;
    arr->size--;

    return ret;
}

uint64_t u64al_pop_front(u64_arrlist_t* arr) {
    if (arr->size == 0) return (uint64_t)-1;
    uint64_t ret = arr->data[0];

    if (arr->size > 1) {
        memmove(arr->data, arr->data + 1, sizeof(uint64_t) * (arr->size - 1));
    }
    arr->size--;
    return ret;
}
// ================================== u32 al =================================

g_arrlist_t* gal_alloc(size_t initial_capacity, size_t chunk_size) {
    g_arrlist_t* ret = malloc(sizeof(g_arrlist_t));
    ret->data = malloc(chunk_size * initial_capacity);
    memset(ret->data, 0, chunk_size * initial_capacity);
    ret->capacity = initial_capacity;
    ret->chunk_size = chunk_size;
    ret->size = 0;

    return ret;
}

void gal_dealloc(g_arrlist_t** arr) {
    if (!(*arr)) return;
    free((*arr)->data);
    free((*arr));
    *arr = NULL;
}

void gal_set_at(g_arrlist_t* arr, int n, void* data) {
    // Cast to char* to ensure the addition is in bytes
    uint8_t* target = (uint8_t*)arr->data + (n * arr->chunk_size);
    memcpy(target, data, arr->chunk_size);
}

void gal_get_at(g_arrlist_t* arr, int n, void* buff) {
    uint8_t* source = (uint8_t*)arr->data + (n * arr->chunk_size);
    memcpy(buff, source, arr->chunk_size);

}

void gal_append(g_arrlist_t* arr, void* entry) {
    if (arr->size >= arr->capacity) {
        arr->data = realloc(arr->data, arr->chunk_size * 2 * arr->capacity);
        arr->capacity *= 2;
    }
    memcpy((uint8_t*)arr->data + (arr->chunk_size * arr->size++), entry, arr->chunk_size);
    // You want: (size - 1) * chunk_size
}



int gal_cmp_at(g_arrlist_t* arr, int n, void* cmp_to) {
    uint8_t* target = (uint8_t*)arr->data + (n * arr->chunk_size);
    int ret =  memcmp(target, cmp_to, arr->chunk_size);
    return ret;
}

void gal_trim(g_arrlist_t* arr, int _set_zero) {
    if (arr->size == 0) return;
    if (_set_zero) memset((uint8_t*)arr->data + (arr->chunk_size * arr->size - 1), 0, arr->chunk_size);
    arr->size--;
}

int gal_pop(g_arrlist_t* arr, void* buffer) {
    if (arr->size == 0) return 0;
    memcpy(buffer, (uint8_t*)arr->data + arr->chunk_size * (arr->size - 1), arr->chunk_size);
    arr->size--;
    return 1;
}

///////////////////////////

#define MAX(a, b) (a >= b)? a : b 

// Initial capacity 3 + task capacity
fd_map_t* fd_map_alloc() {
    worker_t* w = tl_worker;
    size_t cap = 3 + w->tasks.capacity;
    fd_map_t* ret = malloc(sizeof(fd_map_t));
    ret->data = malloc(sizeof(task_handle_t) * (cap));
    ret->occ = malloc(cap);
    memset(ret->occ, 0, cap);
    ret->size = 0;
    ret->capacity = cap;
    return ret;
}

void fdm_free(fd_map_t** fdm) {
    if (!(*fdm)) return;
    free((*fdm)->data);
    free((*fdm)->occ);
    free((*fdm));
    *fdm = NULL;
}

int fdm_add_entry(fd_map_t* fdm, int fd, task_handle_t th) {
    if ((size_t)fd >= fdm->capacity) {
        size_t new_cap = MAX((size_t)fd, fdm->capacity * 2);
        fdm->data = realloc(fdm->data, new_cap);
        fdm->occ = realloc(fdm->occ, new_cap);
        memset(fdm->occ + fdm->capacity, 0, new_cap - fdm->capacity);
        fdm->capacity = new_cap;
    }

    if (fdm->data[fd] == th && fdm->occ[fd] == 1) {
        //Colision
        return 0;
    }

    fdm->data[fd] = th;
    fdm->size++;
    fdm->occ[fd] = 1;
    return 1;
}

inline int fdm_remove_entry(fd_map_t* fdm, int fd) {
    if (!fdm->occ[fd]) return 0;
    fdm->occ[fd] = 0; return 1;
    fdm->size--;
}

inline int fdm_contains(fd_map_t* fdm, int fd) { return (fdm->occ[fd] == 1); }
