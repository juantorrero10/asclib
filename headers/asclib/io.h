#ifndef ASCLIB_IO_H_
#define ASCLIB_IO_H_

#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/uio.h>


task_handle_t asc_write(int fd, void* buff, size_t __n);
task_handle_t asc_read(int fd, void* buff, size_t __n);
task_handle_t asc_sendfile(int out_fd, int in_fd, size_t total_size);
int asc_open(const char* path, int flags, mode_t mode);
task_handle_t asc_readv(int fd, const struct iovec *_iovec, int vector_size);
task_handle_t asc_writev(int fd, const struct iovec *_iovec, int vector_size);



#endif // ASC_IO_H_