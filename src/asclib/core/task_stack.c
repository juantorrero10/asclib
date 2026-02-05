#define _GNU_SOURCE
#include <signal.h>
#include <sys/mman.h>
#include <assert.h>


//#define __DEBUG
//#define __DEBUG_ALLOC 
//#define __DEBUG_DEALLOC
 
#include <defs.h>
#include <task_control.h>
#include <dispatcher.h>
#include <ascerr.h>
#include <log.h>
#include <task_stack.h>
#include <collections.h>

// Managed by task_mem.c
extern TLS g_arrlist_t* generation_map;

// Can alloc an extra guard_page for overflow detection.
// The guard page allocation is only recommended when allocating a whole page multiple (4kb)
// This page isn't actually mapped to physical RAM just a 'trap'
void create_stack_frame(size_t stack_size, TCB* t, uint32_t alloc_guard) {

    if (stack_size == 0) asc_error(PANIC_NO_STACK, "create_stack_frame()", (any_t)0);
    
    if (alloc_guard > 1) alloc_guard = 1;
    uint8_t* stack_temp = mmap(
        NULL, stack_size + (alloc_guard * PAGE_SIZE), PROT_NONE, 
        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0); 

    assert(stack_temp);

    uint8_t* stack = stack_temp + (alloc_guard * PAGE_SIZE);
    mprotect(stack, stack_size, PROT_READ | PROT_WRITE);


    uint8_t* sp = stack + stack_size;

    
    // Align 16 bytes for ABI
    sp = (uint8_t*)((uintptr_t)sp & ~0xF);

    #ifdef __DEBUG_ALLOC
    if (t->guard_page)
        OKAY("Guard page -> (%p - %p), Stack (%p - %p), total %zu", stack - PAGE_SIZE, stack - 1, stack, sp, stack_size + alloc_guard * PAGE_SIZE);
    else
        OKAY("Stack (%p - %p) total %zu", stack, sp, stack_size);
    #endif

    // Push fake rbp
    sp -= sizeof(void*);
    *(void**)sp = (void*)0xBF;

    // Push fake return address -> trampoline
    sp -= sizeof(void*);
    *(void**)sp = task_trampoline;

    t->stack = stack;
    t->stack_size = stack_size;
    t->guard_page = alloc_guard;
    t->ctx.rsp = sp;
    t->ctx.rbp = 0;
    
}

void dealloc_stack(TCB* t) {

    assert(t->stack);

    uint8_t* stack_origin = t->stack      - (t->guard_page * PAGE_SIZE);
    size_t size           = t->stack_size + (t->guard_page * PAGE_SIZE);

#ifdef __DEBUG_DEALLOC
    INFO("dealloc (%p - %p) total: %zu", stack_origin, stack_origin + size, size);
#endif

    munmap(stack_origin, size);
}

void sigsegv_stackoverflow(int sigs, siginfo_t* si, void* unused) {

    (void)unused;
    worker_t* w = tl_worker;
    if (!w) {
        signal(sigs, SIG_DFL);
        raise(sigs);
        return;
    }

    TCB* t = w->tasks.arr[w->tasks.current];
    uintptr_t fault_addr = (uintptr_t)si->si_addr;
    uintptr_t guard_start = (uintptr_t)t->stack - (t->guard_page * PAGE_SIZE);
    uintptr_t guard_end = (uintptr_t)t->stack;

    // Correct check: Did the fault happen inside the task's specific guard page?
    if (t->guard_page && fault_addr >= guard_start && fault_addr < guard_end) {
        char buff[50];
        sprintf(buff, "task<0x%lx>: %p", GET_FULL_HANDLE(), t->fptr);
        asc_error(PANIC_STACK_OVERFLOW_GUARDPAGE, buff, (any_t)fault_addr);
    } else {
        signal(sigs, SIG_DFL);
        raise(sigs);
    }
}

void setup_emergency_stack() {
    stack_t ss;
    ss.ss_sp = malloc(SIGSTKSZ); // Allocate a small separate buffer
    ss.ss_size = SIGSTKSZ;
    ss.ss_flags = 0;
    if (sigaltstack(&ss, NULL) == -1) {
        perror("sigaltstack");
    }
    assert(tl_worker);
    tl_worker->emergency_stack = ss.ss_sp;
}


void set_sigsegv_handler() {
    setup_emergency_stack();
    struct sigaction sa;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK; // <--- Emergency stack
    sa.sa_sigaction = sigsegv_stackoverflow;
    sigaction(SIGSEGV, &sa, NULL);

}