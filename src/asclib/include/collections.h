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
    task_handle_t th;
    uint64_t timeout;
} timeout_t;

typedef struct {
    timeout_t* data;
    size_t size;
    size_t capacity;
} timer_heap_t;

typedef struct {
    uint64_t* data;
    size_t size;
    size_t capacity; 
} u64_arrlist_t;

typedef struct _Generic_Array_List {
    void* data;
    size_t chunk_size;
    size_t size;
    size_t capacity; 
} g_arrlist_t;

uu_hash_map* hm_alloc(size_t capacity);
void hm_dealloc(uu_hash_map** hashmap);
int hm_add_entry(uu_hash_map* hm, size_t k, size_t v);
int hm_remove_entry(uu_hash_map* hm, size_t fd);
size_t hm_get_v(uu_hash_map* hm, size_t k);


timer_heap_t* heap_alloc(size_t initial_capacity);
void heap_dealloc(timer_heap_t** h);
void heap_push(timer_heap_t* hp, size_t task_id, uint64_t timeout);
timeout_t heap_pop(timer_heap_t* hp);

u64_arrlist_t* u64al_alloc(size_t initial_capacity);
void u64al_dealloc(u64_arrlist_t** arr);
void u64al_append(u64_arrlist_t* arr, uint64_t entry);
uint64_t u64al_pop(u64_arrlist_t* arr, int _set_zero);
uint64_t u64al_pop_front(u64_arrlist_t* arr);

g_arrlist_t* gal_alloc(size_t initial_capacity, size_t chunk_size);
void gal_dealloc(g_arrlist_t** arr);
void gal_append(g_arrlist_t* arr, void* entry);
void gal_trim(g_arrlist_t* arr, int _set_zero);
void gal_set_at(g_arrlist_t* arr, int n, void* data);
void gal_get_at(g_arrlist_t* arr, int n,  void* buff);
int gal_cmp_at(g_arrlist_t* arr, int n, void* cmp_to);
int gal_pop(g_arrlist_t* arr, void* buffer);


#endif // HASHMAP_H_