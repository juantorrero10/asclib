#ifndef ASCLIB_IO_H_
#define ASCLIB_IO_H_

#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>

task_handle asc_read(int fd, void* buff, size_t __n);
task_handle asc_write(int fd, void* buff, size_t __n);
int asc_open(const char* path, int flags, mode_t mode);


#endif // ASC_IO_H_