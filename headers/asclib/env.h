#ifndef DEMO_ASCLIB_ENV_H_
#define DEMO_ASCLIB_ENV_H_

#include <stdint.h>
#include <stdlib.h>

typedef struct _AL_TH_ task_handle_t;

uint64_t _env_new_tls_key(size_t sz, void(*destructor)(void*));
void _env_local_get(uint64_t key, void* buffer);
void _env_local_set(uint64_t key, void* val);

#define ENV_DECLARE(type, name) \
    static uint64_t __tls_idx_##name = -1; \
    __attribute__((constructor)) void __init_tls_##name() { \
        __tls_idx_##name = _env_new_tls_key(sizeof(type), NULL); \
    } \
    static inline type ENVGET_##name() { \
        type __tmp; _env_local_get(__tls_idx_##name, &__tmp);\
        return __tmp; \
    } \
    static inline void ENVSET_##name(type val) { \
        type __temp = val;\
        _env_local_set(__tls_idx_##name, (void*)&__temp); \
    }

#define ENV_DECLARE_DYN(type_deref, name, destructor) \
    static uint64_t __tls_idx_##name = -1; \
    __attribute__((constructor)) void __init_tls_##name() { \
        __tls_idx_##name = _env_new_tls_key(sizeof(type_deref*), destructor); \
    } \
    static inline type_deref* ENVGET_##name() { \
        type_deref* __tmp; _env_local_get(__tls_idx_##name, &__tmp);\
        return __tmp; \
    } \
    static inline void ENVSET_##name(type_deref* val) { \
        type_deref* __temp = val;\
        _env_local_set(__tls_idx_##name, (void*)&__temp); \
    }\
    static inline void ENVSET_##name##_DEREF(type_deref val) {\
        type_deref* __tmp; _env_local_get(__tls_idx_##name, &__tmp);\
        *__tmp = val;\
    }

#define ENV_DECLARE_NOSET(type, name) \
    static uint64_t __tls_idx_##name = -1; \
    __attribute__((constructor)) void __init_tls_##name() { \
        __tls_idx_##name = _env_new_tls_key(sizeof(type), NULL); \
    } \
    static inline type ENVGET_##name() { \
        type __tmp; _env_local_get(__tls_idx_##name, &__tmp);\
        return __tmp; \
    } \
    

// Hardcoded keys
ENV_DECLARE_NOSET(task_handle_t, SELF); // KEY = 0

#endif // DEMO_ASCLIB_ENV_H_