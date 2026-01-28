#ifndef HASHMAP_H_
#define HASHMAP_H_

#include <stdlib.h>
#include <stdint.h>
#include <defs/worker.h>

typedef struct {
    size_t k;         
    size_t v;
} uu_hash_entry;

typedef struct {
    uu_hash_entry* map;
    size_t size;
    size_t capacity;
    uint8_t* occ;
} uu_hash_map;

typedef struct {
    uint64_t* data;
    size_t size;
    size_t capacity;
} heap_t;

uu_hash_map* hm_alloc(size_t capacity);
void hm_dealloc(uu_hash_map** hashmap);
int hm_add_entry(uu_hash_map* hm, size_t k, size_t v);
int hm_remove_entry(uu_hash_map* hm, size_t fd);
size_t hm_get_v(uu_hash_map* hm, size_t k);


heap_t* heap_alloc(size_t initial_capacity);
void heap_dealloc(heap_t** h);
uint64_t heap_peek(worker_t* w, heap_t* hp);
void heap_push(worker_t* w, heap_t* hp, size_t task);


#endif // HASHMAP_H_