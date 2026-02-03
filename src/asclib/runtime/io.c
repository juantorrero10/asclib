#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <assert.h>
#include <string.h>
#include <netinet/in.h>
#include <signal.h>

//#define __DEBUG

#include <defs.h>
#include <task_control.h>
#include <task_mem.h>
#include <ascerr.h>
#include <log.h>
#include <runtime.h>
#include <collections.h>

#define ENABLE_GUARD_PAGE 1

#define HASHMAP_INIT_CAPACITY 6
TLS uu_hash_map* fdh = NULL;

//Forward decl
static void set_nonblock(int fd);
static void set_block(int fd);


#define task_return(var, type) \
    do { \
        type __temp = var; \
        return __task_alloc_ret_val(sizeof(type), &__temp); \
    } while(0)

struct _Async_Read_Params{
    int fd;
    void* buff;
    size_t n;
    int toggle_blocking;
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

    free(ap);

    claim_fd(fd, w->tasks.current);
    if(r) set_nonblock(fd);

    
    for (;;) {
        ssize_t n = read(fd, buff, sz);
        if (n >= 0)  {
            release_fd(fd); 
            if (r)set_block(fd); 
            task_return(n, ssize_t); 
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            __task_wait_on_fd(fd, EPOLLIN);
            continue;
        }
        release_fd(fd);
        if (r) set_block(fd);
        task_return(-1, ssize_t);
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

    claim_fd(fd, w->tasks.current);
    if (r) set_nonblock(fd);

    for (;;) {
        ssize_t n = (send_or_write)
            ? write(fd, p + written_so_far, total_to_write - written_so_far)
            : send(fd, p + written_so_far, total_to_write - written_so_far, MSG_NOSIGNAL);
        if (n >= 0) {
            written_so_far += n;

            //Full write
            if (written_so_far >= total_to_write) {
                release_fd(fd);
                if (r) set_block(fd);
                task_return(written_so_far, ssize_t);
            }   
            //Partials write -> yield
            __task_wait_on_fd(fd, EPOLLOUT);
            continue;
        }

        if (n == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                __task_wait_on_fd(fd, EPOLLOUT);
                continue;
            }
            // Erorr
            if (r) set_block(fd);
            release_fd(fd);
            task_return(-1, ssize_t);
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

    task_entry_t te = {
        .fptr = __internal_read,
        .value = ap,
        .arg_size = sizeof(void*),
        .n_pages = 0,
    };

    return __task_create_ex(
        te, 4096, ENABLE_GUARD_PAGE);
}



void claim_fd(int fd, size_t task) {

    if (!fdh) {
        fdh = hm_alloc(HASHMAP_INIT_CAPACITY);
    }

    assert(fdh);

    int n = hm_add_entry(fdh, fd, task);

    if (n == 0) {
        asc_io_error(IO_PANIC_LOCKED_FD, "IO::claim_fd()", (any_t)fd, (any_t)task);
    };
}

void release_fd(int fd) {
    assert(fdh);
    assert(hm_remove_entry(fdh, (size_t)fd));
}

void __io_free_hashmap() {
    hm_dealloc(&fdh);
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

