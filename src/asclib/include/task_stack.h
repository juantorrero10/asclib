#ifndef TASK_STACK_H_
#define TASK_STACK_H_


#include <defs.h>

#define PAGE_SIZE 4096

// Can alloc an extra guard_page for overflow detection.
// The guard page allocation is only recommended when allocating a whole page multiple (4kb)
// This page isn't actually mapped to physical RAM just a 'trap'
void create_stack_frame(size_t stack_size, TCB* t, uint32_t alloc_guard);

void dealloc_stack(TCB* t);

void set_sigsegv_handler();


#endif // TASK_STACK_H_