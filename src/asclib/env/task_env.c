#include <string.h>

#include <dispatcher.h>
#include <collections.h>
#include <task_control.h>
#include <ascerr.h>

// Global variable, maps key to memory index
u64_arrlist_t* _env_tls_idx_table = NULL;
uu_hash_map* _env_tls_desctructor_table = NULL;
size_t _env_total_stack_size = 0;

extern TLS g_arrlist_t* generation_map;

#define GET_SIZE(key) ({\
    uint64_t sz = (key == 0)? _env_tls_idx_table->data[key] : (_env_tls_idx_table->data[key] - _env_tls_idx_table->data[key - 1]);\
    (size_t)sz;\
})

#define INITIAL_TLS_SIZE 4

static void __set_hardcoded_keys();

static void _env_tls_global_teardown(void) {
    if (_env_tls_idx_table) {
        u64al_dealloc(&_env_tls_idx_table);
    }
    if (_env_tls_desctructor_table) {
        hm_dealloc(&_env_tls_desctructor_table);
    }
}


uint64_t _env_new_tls_key(size_t sz, void(*destructor)(void*)) {
    if (!_env_tls_idx_table) {
        _env_tls_idx_table = u64al_alloc(INITIAL_TLS_SIZE);
    }
    if (!_env_tls_desctructor_table) {
        _env_tls_desctructor_table = hm_alloc(INITIAL_TLS_SIZE);
    }

    u64al_append(_env_tls_idx_table, (uint64_t)(_env_total_stack_size + sz));
    uint64_t new_key = (size_t)_env_tls_idx_table->size - 1;
    if (destructor != NULL) hm_add_entry(_env_tls_desctructor_table, new_key, (uintptr_t)destructor);
    // Tear down these structures at the end of the program
    atexit(_env_tls_global_teardown);

    _env_total_stack_size += sz;

    return new_key;
}



void _env_local_get(uint64_t key, void* buffer) {
    worker_t* w = tl_worker;
    ASSERT_CONTEXT(w, "_env_local_get()");
    TCB* t = w->tasks.arr[w->tasks.current];

    uint64_t idx = (key > 0)? _env_tls_idx_table->data[key - 1] : 0;
    size_t size = (size_t)(_env_tls_idx_table->data[key] - idx);

    // Is only allocated if tls is used
    if (!t->tls) {
        t->tls = calloc(1, _env_total_stack_size);
        // Its going to zero anyway
        __set_hardcoded_keys();
    }

    memcpy(buffer, (uint8_t*)t->tls + idx, size);
}

void _env_local_set(uint64_t key, void* val) {
    worker_t* w = tl_worker;
    ASSERT_CONTEXT(w, "_env_local_get()");
    TCB* t = w->tasks.arr[w->tasks.current];

    uint64_t idx = (key > 0)? _env_tls_idx_table->data[key - 1] : 0;
    size_t size = (size_t)(_env_tls_idx_table->data[key] - idx);

    if (key < 1) {
        asc_error(WARN_KEY_READONLY, "_env_local_set", (any_t)key);
        return;
    }

    // Is only allocated if tls is used
    if (!t->tls) {
        t->tls = calloc(1, _env_total_stack_size);
        __set_hardcoded_keys();
    }

    memcpy((uint8_t*)t->tls + idx, val, size);
}

static void __set_hardcoded_keys() {
    worker_t* w = tl_worker;
    uint64_t t = GET_FULL_HANDLE();
    memcpy(w->tasks.arr[w->tasks.current]->tls,
        &t, sizeof(uint64_t));
}

static void __env_local_get_of(uint64_t key, void* buffer, TCB* t) {

    uint64_t idx = (key > 0)? _env_tls_idx_table->data[key - 1] : 0;
    size_t size = (size_t)(_env_tls_idx_table->data[key] - idx);

    // Is only allocated if tls is used
    if (!t->tls) {
        return;
    }

    memcpy(buffer, (uint8_t*)t->tls + idx, size);
}

void _env_call_destructors(TCB* t)  {
    void (*curr)(void*) = NULL;
    void* ptr = NULL;
    for (size_t i = 0; i < _env_tls_desctructor_table->size; i++)
    {
        uint64_t key = _env_tls_desctructor_table->map[i].k;
        curr =  (void (*)(void*))_env_tls_desctructor_table->map[i].v;
        __env_local_get_of(key, &ptr, t);
        if (ptr != NULL) curr(ptr);
        ptr = NULL;
    }
    
}

