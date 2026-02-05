#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <assert.h>
#include <string.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/sendfile.h>
#include <sys/uio.h>


#define __DEBUG

#include <user_lib.h>
#include <defs.h>
#include <task_control.h>
#include <task_mem.h>
#include <ascerr.h>
#include <log.h>
#include <runtime.h>
#include <collections.h>

#define ENABLE_GUARD_PAGE 1

#define HASHMAP_INIT_CAPACITY 6
TLS fd_map_t* fdh = NULL;

//Forward decl
static void set_nonblock(int fd);
static void set_block(int fd);

extern TLS g_arrlist_t* generation_map;


struct _Async_Read_Params{
    int fd;
    void* buff;
    size_t n;
    int toggle_blocking;
    int read_or_recv;
};

struct _Async_Write_Params {
    int fd;
    const void* buff;
    size_t n;
    int toggle_blocking;
    int send_or_write;
};

static void* __internal_read(void* __arg) {
    worker_t* w = tl_worker; assert(w);
    struct _Async_Read_Params* ap = (struct _Async_Read_Params*)__arg;
    int fd = ap->fd;
    void* buff = ap->buff;
    size_t sz = ap->n;
    int r = ap->toggle_blocking; (void)r;
    int o = ap->read_or_recv;

    free(ap);

    __claim_fd(fd, GET_FULL_HANDLE(), EPOLLIN);
    if(r) set_nonblock(fd);

    
    for (;;) {
        ssize_t n = (o)
            ? recv(fd, buff, sz, MSG_DONTWAIT | MSG_NOSIGNAL)
            : read(fd, buff, sz);
        if (n >= 0)  {
            if (r)set_block(fd);
            __release_fd(fd);
            task_return(ssize_t, n); 
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            __task_wait_on_fd(fd);
            yield;
            continue;
        }
        __release_fd(fd);
        if (r) set_block(fd);
        task_return(ssize_t, -1);
    }
}

static void* __internal_write(void* __arg) {
    worker_t* w = tl_worker; assert(w);
    struct _Async_Write_Params* ap = (struct _Async_Write_Params*)__arg;
    int fd = ap->fd;
    const char* p = (const char*)ap->buff;
    size_t total_to_write = ap->n;
    size_t written_so_far = 0;
    int r = ap->toggle_blocking; (void)r;
    int send_or_write = ap->send_or_write;

    free(ap);

    __claim_fd(fd, GET_FULL_HANDLE(), EPOLLOUT);
    if (r) set_nonblock(fd);

    for (;;) {
        ssize_t n = (send_or_write)
            ? write(fd, p + written_so_far, total_to_write - written_so_far)
            : send(fd, p + written_so_far, total_to_write - written_so_far, MSG_NOSIGNAL | MSG_DONTWAIT);
        if (n >= 0) {
            written_so_far += n;

            //Full write
            if (written_so_far >= total_to_write) {
                __release_fd(fd);
                if (r) set_block(fd);
                task_return(ssize_t, written_so_far);
            }   
            //Partials write -> yield
            __task_wait_on_fd(fd);
            continue;
        }

        if (n == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                __task_wait_on_fd(fd);
                continue;
            }
            // Erorr
            if (r) set_block(fd);
            __release_fd(fd);
            task_return(ssize_t, -1);
        }

    }
}


int asc_open(const char* path, int flags, mode_t mode) {
    int fd = open(path, flags | O_NONBLOCK, mode);
    if (fd == -1) {
        char buff[50];
        sprintf(buff, "%s -> \"%s\"", strerror(errno), path);
        asc_io_error(IO_ERROR_FILE_NOT_FOUND, "asc_open()", (any_t)(void*)buff, (any_t)0); 
    }
    return fd;
}


task_handle_t asc_write(int fd, void* buff, size_t __n) {
    struct _Async_Write_Params* ap = malloc(sizeof(struct _Async_Write_Params));
    ap->fd = fd;
    ap->buff = buff;
    ap->n = __n;
    ap->send_or_write = 1;

    ap->toggle_blocking = (fd == 1 || fd == 2) ? 1 : 0;

    task_entry_t te = {
        .fptr = __internal_write,
        .value = ap,
        .arg_size = sizeof(void*),
        .n_pages = 0,
    };

    return __task_create_ex(
        te, 4096, ENABLE_GUARD_PAGE
    );
}

task_handle_t asc_send(int fd, void* buff, size_t __n) {
    struct _Async_Write_Params* ap = malloc(sizeof(struct _Async_Write_Params));
    ap->fd = fd;
    ap->buff = buff;
    ap->n = __n;
    ap->send_or_write = 0;

    ap->toggle_blocking = (fd >= 1 && fd <= 2);

    task_entry_t te = {
        .fptr = __internal_write,
        .value = ap,
        .arg_size = sizeof(void*),
        .n_pages = 0,
    };

    return __task_create_ex(
        te, 4096, ENABLE_GUARD_PAGE
    );
}

task_handle_t asc_read(int fd, void* buff, size_t __n) {
    struct _Async_Read_Params* ap = malloc(sizeof(struct _Async_Read_Params));

    ap->fd = fd;
    ap->buff = buff;
    ap->n = __n;
    ap->toggle_blocking = (fd == 0);
    ap->read_or_recv = 0;

    task_entry_t te = {
        .fptr = __internal_read,
        .value = ap,
        .arg_size = sizeof(void*),
        .n_pages = 0,
    };

    return __task_create_ex(
        te, 4096, ENABLE_GUARD_PAGE);
}

task_handle_t asc_recv(int fd, void* buff, size_t __n) {
    struct _Async_Read_Params* ap = malloc(sizeof(struct _Async_Read_Params));

    ap->fd = fd;
    ap->buff = buff;
    ap->n = __n;
    ap->toggle_blocking = (fd == 0);
    ap->read_or_recv = 1;

    task_entry_t te = {
        .fptr = __internal_read,
        .value = ap,
        .arg_size = sizeof(void*),
        .n_pages = 0,
    };

    return __task_create_ex(
        te, 4096, ENABLE_GUARD_PAGE);
}



void __claim_fd(int fd, size_t task, uint32_t events) {

    if (!fdh) {
        fdh = fd_map_alloc();
    }

    worker_t* w = tl_worker;

    int n = fdm_add_entry(fdh, fd, task);

    struct epoll_event ev = { .events = events | EPOLLONESHOT, .data.u64 = GET_FULL_HANDLE() };

    if (epoll_ctl(w->sync.epoll_fd, EPOLL_CTL_MOD, fd, &ev) == -1) {
        if (errno == ENOENT) {
            epoll_ctl(w->sync.epoll_fd, EPOLL_CTL_ADD, fd, &ev);
        }
    }

    if (n == 0) {
        asc_io_error(IO_PANIC_LOCKED_FD, "IO::__claim_fd()", (any_t)fd, (any_t)task);
    };
}

void __release_fd(int fd) {
    assert(fd >= 0);
    worker_t* w = tl_worker;
    assert(fdh);
    epoll_ctl(w->sync.epoll_fd, EPOLL_CTL_DEL, fd, NULL);
    assert(fdm_remove_entry(fdh, fd));
}

void __io_free_fdmap() {
    fdm_free(&fdh);
}


static void set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (!(flags & O_NONBLOCK)) {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
}

static void set_block(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if ((flags & O_NONBLOCK)) {
        fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
    }
}

async(__internal_sendfile, int out_fd; int in_fd; size_t total_size) {
    size_t total_size = arg(total_size);
    int in_fd = arg(in_fd);
    int out_fd = arg(out_fd);
    off_t current_offset = 0;
    worker_t* w = tl_worker;
    __claim_fd(out_fd, GET_FULL_HANDLE(), EPOLLOUT);
    // Assuming out_fd (socket) is already non-blocking from the accept_loop

    for (;;) {
        // IMPORTANT: sendfile updates current_offset internally!
        // We ask to send (Total - current_offset) bytes.
        ssize_t n = sendfile(out_fd, in_fd, &current_offset, total_size - (size_t)current_offset);

        if (n >= 0) {
            // Success! current_offset was updated by the kernel.
            if ((size_t)current_offset >= total_size) {
                __release_fd(out_fd);
                task_return(ssize_t, (ssize_t)current_offset);
            }
            
            // Partial write: wait for socket buffer space
            __task_wait_on_fd(out_fd);
            continue;
        }

        if (n == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                __task_wait_on_fd(out_fd);
                continue;
            }
            // Real Error
            __release_fd(out_fd);
            task_return(ssize_t, -1);
        }
    }
    end_return_void();
}
task_handle_t asc_sendfile(int out_fd, int in_fd, size_t total_size) {
    return task_spawn_ex(__internal_sendfile, 4096, 1, .out_fd = out_fd, .in_fd = in_fd, .total_size = total_size);
}

async(__internal_readv, int fd; const struct iovec *_iovec; int vector_size; int toggle_blocking) {
    int fd =                      arg(fd);
    const struct iovec* __iovec = arg(_iovec);
    int vector_size =             arg(vector_size);
    int r =                       arg(toggle_blocking);

    worker_t* w = tl_worker;

    __claim_fd(fd, GET_FULL_HANDLE(), EPOLLIN);
    if (r) set_nonblock(fd);
    while(1) {
        ssize_t n = readv(fd, __iovec, vector_size);
        if (n >= 0) {
            if (r) set_block(fd);
            __release_fd(fd);
            task_return(ssize_t, n);
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            __task_wait_on_fd(fd);
            yield;
            continue;
        }

        if (n <= 0) {
            if (r) set_block(fd);
            __release_fd(fd);
            task_return(ssize_t, -1);
        }
    }
    end_return_void();
}

async( __internal_recvmsg, int fd; struct msghdr *message) {
    int fd                  = arg(fd);
    struct msghdr* msg      = arg(message);
    worker_t* w = tl_worker;

    __claim_fd(fd, GET_FULL_HANDLE(), EPOLLIN);
    while(1) {
        ssize_t n = recvmsg(fd, msg, MSG_DONTWAIT | MSG_NOSIGNAL);

        if (n >= 0) {
            __release_fd(fd);
            task_return(ssize_t, n);
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            __task_wait_on_fd(fd);
            yield;
            continue;
        }
        if (n <= 0) {
            __release_fd(fd);
            task_return(ssize_t, -1);
        }
    }
    end_return_void();
}

static void advance_iov(struct iovec** iov, int* iovcnt, ssize_t n) {
    while (*iovcnt > 0 && n > 0) {
        if ((size_t)n >= (*iov)->iov_len) {
            n -= (*iov)->iov_len;
            (*iov)++;
            (*iovcnt)--;
        } else {
            (*iov)->iov_base = (char*)(*iov)->iov_base + n;
            (*iov)->iov_len -= n;
            break;
        }
    }
}

async(__internal_writev, int fd; const struct iovec *vecs; int vector_size; int toggle_blocking) {
    int fd = arg(fd);
    const struct iovec *vecs = arg(vecs);
    int vsz = arg(vector_size);
    int tb = arg(toggle_blocking);

    ssize_t total_writen = 0;
    worker_t* w = tl_worker;

    __claim_fd(fd, GET_FULL_HANDLE(), EPOLLOUT);
    if (tb) set_nonblock(fd);

    struct iovec* ptr = (struct iovec*) vecs;

    while (vsz > 0) {
        ssize_t n = writev(fd, ptr, vsz);

        if (n > 0) {
            total_writen += n;
            advance_iov(&ptr, &vsz, n);
            continue;
        }

        if (n == 0) {
            if (tb) set_block(fd);
            __task_wait_on_fd(fd);
            continue;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            if (tb) set_block(fd);
            __task_wait_on_fd(fd);
            continue;
        }

        if ( n < 0) {
            if (tb) set_block(fd);
            __release_fd(fd);
            task_return(ssize_t, -1);
        }
    }
    if (tb) set_block(fd);
    __release_fd(fd);
    end_return(ssize_t, total_writen);
}


async(__internal_sendmsg, int fd; const struct msghdr *msg) {
    int fd = arg(fd);
    const struct msghdr* msg = arg(msg);

    worker_t* w = tl_worker;

    __claim_fd(fd, GET_FULL_HANDLE(), EPOLLOUT);

    size_t total_writen = 0;

    struct iovec* iov = msg->msg_iov;
    size_t iov_len = msg->msg_iovlen;

    while (iov_len > 0) {
        ssize_t n = sendmsg(fd, msg, MSG_NOSIGNAL | MSG_DONTWAIT);

        if (n > 0) {
            total_writen += n;
            advance_iov(&iov, (int*)&iov_len, n);
            continue;
        }

        if (n == 0) {
            __task_wait_on_fd(fd);
            continue;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            __task_wait_on_fd(fd);
            continue;
        }

        if ( n < 0) {
            __release_fd(fd);
            task_return(ssize_t, -1);
        }
    }
    __release_fd(fd);
    end_return(ssize_t, total_writen);
}



task_handle_t asc_readv(int fd, const struct iovec *_iovec, int vector_size) {
    return task_spawn_ex(__internal_readv, 4096, 1, 
        .fd = fd, ._iovec = _iovec, .vector_size = vector_size, .toggle_blocking = (fd <= 2) );
}


task_handle_t asc_writev(int fd, const struct iovec *_iovec, int vector_size) {
    return task_spawn_ex(__internal_writev, 4096, 1, 
        .fd = fd, .vecs = _iovec, .vector_size = vector_size, .toggle_blocking = (fd <= 2) );
}

task_handle_t asc_recvmsg(int fd, struct msghdr *msg) {
    return task_spawn_ex(__internal_recvmsg, 4096, 1, 
        .fd = fd, .message = msg);
}

task_handle_t asc_sendmsg(int fd, const struct msghdr *msg) {
    return task_spawn_ex(__internal_sendmsg, 4096, 1, 
        .fd = fd, .msg = msg);
}
