#include <string.h>
#include <collections.h>
#include <defs/worker.h>

/*------------------------------HASH MAPS ------------------------- */
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

    if ((size_t)found >= hm->highest_index) hm->highest_index = found - 1;

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

// =========================== HEAP ==============================================0

heap_t* heap_alloc(size_t initial_capacity) {
    heap_t* ret = malloc(sizeof(heap_t));
    ret->data = malloc(initial_capacity * sizeof(uint64_t));
    ret->capacity = initial_capacity;
    ret->size = 0;
    return ret;
}

void heap_dealloc(heap_t** h) {
    free((*h)->data);
    free((*h));
    *h = NULL;
}

static uint64_t get_ts(worker_t* w, size_t task) {
    // Safety check: Virtual tasks (NULL) or empty slots
    if (task >= w->tasks.capacity || !w->tasks.arr[task]) {
        // Return "Infinity" so these invalid nodes sink to the bottom of the Min-Heap
        return (uint64_t)-1; 
    }
    return w->tasks.arr[task]->wait_for.timestamp;
}

uint64_t heap_peek(worker_t* w, heap_t* hp) {
    uint64_t ret = hp->data[0];
    if (hp->size == 0) return (uint64_t)-1;

    uint64_t last_h = hp->data[--hp->size];
    size_t i = 0;

    // Shift Down
    while (i * 2 + 1 < hp->size) {
        size_t child = i * 2 + 1;
        if (child + 1 < hp->size && get_ts(w, hp->data[child + 1]) < get_ts(w, hp->data[child])) {
            child++;
        }
        if (get_ts(w, last_h) <= get_ts(w, hp->data[child])) break;
        hp->data[i] = hp->data[child];
        i = child;
    }
    hp->data[i] = last_h;

    return ret;
}

void heap_push(worker_t* w, heap_t* hp, size_t task) {

    if (hp->size >= hp->capacity) {
        hp->data = realloc(hp->data, sizeof(uint64_t) * 2 * hp->capacity);
        hp->capacity *= 2;
    }

    //Shift down
    size_t i = hp->size++;
    while (i > 0) {
        size_t p = (i - 1) / 2;
        if (get_ts(w, hp->data[p]) <= get_ts(w, task)) break;
        hp->data[i] = hp->data[p];
        i = p;
    }
    hp->data[i] = task;
}