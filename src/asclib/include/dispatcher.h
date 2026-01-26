#ifndef DISPATCHER_H_
#define DISPATCHER_H_

#include <defs.h>


// Get Compiler-independent thread local keyword
#if defined(__GNUC__)
#   define TLS __thread
#else
#   define TLS _Thread_local
#endif

extern TLS worker_t* tl_worker;


__attribute__((sysv_abi))
extern void _asm_ctx_switch_64(
    ctx_t* new,
    ctx_t* old
);

void print_ctx(ctx_t* c);

void* dispatcher(void* arg);

#endif // DISPATCHER_H_