#ifndef SLEEP_H_
#define SLEEP_H_

#include <defs.h>

void try_wake_up(TCB* candidate);
uint64_t get_time_ms();

void __claim_fd(int fd, size_t task, uint32_t events);
void __release_fd(int fd);

#endif // SLEEP_H_