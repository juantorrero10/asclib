#ifndef ASCLIB_IO_H_
#define ASCLIB_IO_H_

#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>

task_handle_t asc_read(int fd, void* buff, size_t __n);
task_handle_t asc_recv(int fd, void* buff, size_t __n);
task_handle_t asc_write(int fd, void* buff, size_t __n);
task_handle_t asc_send(int fd, void* buff, size_t __n);
task_handle_t asc_sendfile(int out_fd, int in_fd, size_t total_size);
int asc_open(const char* path, int flags, mode_t mode);


#endif // ASC_IO_H_