#ifndef TASK_MEM_H_
#define TASK_MEM_H_

#include <dispatcher.h>
#include <task_control.h>


void __task_destroy(task_handle_t t);
task_handle_t __task_create(task_entry_t te);
void __alloc_occupation_map();
void __free_occupation_map();
uint8_t _get_occupation_at(size_t id);
void _set_occupation_at(size_t id, uint8_t val);

#endif // TASK_MEM_H_