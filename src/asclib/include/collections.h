#ifndef HASHMAP_H_
#define HASHMAP_H_

#include <stdlib.h>
#include <stdint.h>
#include <defs/worker.h>

typedef size_t task_handle_t;

typedef struct {
    size_t k;         
    size_t v;
} uu_hash_entry;

typedef struct {
    uu_hash_entry* map;
    size_t size;
    size_t capacity;
    size_t highest_index;
    uint8_t* occ;
} uu_hash_map;

typedef struct {
    uint64_t* data;
    size_t size;
    size_t capacity;
} heap_t;

typedef struct {
    uint64_t* data;
    size_t size;
    size_t capacity; 
} u64_arrlist_t;

typedef struct {
    void* data;
    size_t chunk_size;
    size_t size;
    size_t capacity; 
} g_arrlist_t;

typedef struct _ASCLIB_mpsc_Channel {
    uint32_t magic;

    void* buffer;
    size_t packet_size;
    size_t capacity;
    size_t count;
    size_t head, tail;

    // Single consumer -> 1 receiving waiter
    task_handle_t rx_waiter; // The waiter is the spawned task that blocks itself until data is send. 
    task_handle_t rx_owner;  // The owner is the task which is allowed to call recv and move the rx
    // Multiple produces
    u64_arrlist_t* tx_waiters;
    size_t tx_rc;            // Reference count. Calling drop subtracts 1 and frees when reaching 0. 
}mpsc_t;

uu_hash_map* hm_alloc(size_t capacity);
void hm_dealloc(uu_hash_map** hashmap);
int hm_add_entry(uu_hash_map* hm, size_t k, size_t v);
int hm_remove_entry(uu_hash_map* hm, size_t fd);
size_t hm_get_v(uu_hash_map* hm, size_t k);


heap_t* heap_alloc(size_t initial_capacity);
void heap_dealloc(heap_t** h);
uint64_t heap_peek(worker_t* w, heap_t* hp);
void heap_push(worker_t* w, heap_t* hp, size_t task);

u64_arrlist_t* u64al_alloc(size_t initial_capacity);
void u64al_dealloc(u64_arrlist_t** arr);
void u64al_append(u64_arrlist_t* arr, uint64_t entry);
uint64_t u64_trim(u64_arrlist_t* arr, int _set_zero);
uint64_t u64al_pop_front(u64_arrlist_t* arr);

g_arrlist_t* gal_alloc(size_t initial_capacity, size_t chunk_size);
void gal_dealloc(g_arrlist_t** arr);
void gal_append(g_arrlist_t* arr, void* entry);
void gal_trim(g_arrlist_t* arr, int _set_zero);


#endif // HASHMAP_H_