#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <assert.h>
#include <string.h>

//#define __DEBUG

#include <defs.h>
#include <task_control.h>
#include <task_mem.h>
#include <ascerr.h>
#include <log.h>

#define ENABLE_GUARD_PAGE 0

#define HASHMAP_INIT_CAPACITY 6
TLS fd_hash_map* fdh = NULL;

//Forward decl
static void claim_fd(int fd, size_t task);
static void release_fd(int fd);
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
    int reset_doblock;
};

struct _Async_Write_Params {
    int fd;
    const void* buff;
    size_t n;
    int reset_doblock;
};

static void* __internal_read(void* __arg) {
    worker_t* w = tl_worker; assert(w);
    struct _Async_Read_Params* ap = (struct _Async_Read_Params*)__arg;
    int fd = ap->fd;
    void* buff = ap->buff;
    size_t sz = ap->n;
    int r = ap->reset_doblock;

    free(ap);

    claim_fd(fd, w->tasks.current);
    set_nonblock(fd);

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
    int r = ap->reset_doblock;

    free(ap);

    claim_fd(fd, w->tasks.current);
    set_nonblock(fd);

    for (;;) {
        ssize_t n = write(fd, p + written_so_far, total_to_write - written_so_far);

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

    ap->reset_doblock = (fd == 1 || fd == 2) ? 1 : 0;

    return __task_create_ex(
        task_entry_ex(__internal_write, struct _Async_Write_Params*, ap, 0), 4096, ENABLE_GUARD_PAGE
    );
}

task_handle_t asc_read(int fd, void* buff, size_t __n) {
    struct _Async_Read_Params* ap = malloc(sizeof(struct _Async_Read_Params));

    ap->fd = fd;
    ap->buff = buff;
    ap->n = __n;
    ap->reset_doblock = (fd == 0);

    return __task_create_ex(
        task_entry_ex(__internal_read, struct _Async_Read_Params*, ap, 0), 4096, ENABLE_GUARD_PAGE);
}

static int hm_add_entry(fd_hash_map* hm, int fd, size_t task) {

    int first_hole = -1;

    // Search if key is present
    for (size_t i = 0; i < hm->capacity; i++)
    {
        if (hm->occ[i] == 0) { 
            if(first_hole == -1) {
                first_hole = i; 
            } continue; 
        }
        if (hm->map[i].k_fd == fd) { 
            return 0; 
        }
    }

    // Expand if full
    if (first_hole == -1) {
        size_t old_cap = hm->capacity;
        hm->capacity *= 2;

        hm->map = realloc(hm->map, sizeof(fd_hash_entry) * hm->capacity);
        hm->occ = realloc(hm->occ, hm->capacity);
        memset(hm->occ + old_cap, 0, old_cap);
        first_hole = old_cap;
    }

    //Copy
    fd_hash_entry e = { .k_fd = fd, .v_task = task };

    memcpy(hm->map + first_hole, &e, sizeof(fd_hash_entry));
    hm->occ[first_hole] = 1;
    hm->size++;
    return 1;
}

static int hm_remove_entry(fd_hash_map* hm, int fd) {
    int found = -1;

    // Search for key
    for (size_t i = 0; i < hm->capacity; i++)
    {
        if (hm->occ[i] == 0) { continue; }
        if (hm->map[i].k_fd == fd) { found = i; break;}
    }
    if (found == -1) return 0;

    hm->size--;
    hm->occ[found] = 0;

    return 1;
}


static void claim_fd(int fd, size_t task) {

    if (!fdh) {
        fdh = malloc(sizeof(fd_hash_map));
        fdh->map = malloc(HASHMAP_INIT_CAPACITY * sizeof(fd_hash_entry));
        fdh->occ = malloc(HASHMAP_INIT_CAPACITY);
        memset(fdh->occ, 0, HASHMAP_INIT_CAPACITY);
        fdh->capacity = HASHMAP_INIT_CAPACITY;
        fdh->size = 0;
    }

    assert(fdh);

    INFO("fd: %d, task: %zu", fd, task);

    int n = hm_add_entry(fdh, fd, task);

    if (n == 0) {
        asc_io_error(IO_PANIC_LOCKED_FD, "IO::claim_fd()", (any_t)fd, (any_t)task);
    };
}

static void release_fd(int fd) {
    assert(fdh);
    assert(hm_remove_entry(fdh, fd));
}

void __io_free_hashmap() {
    assert(fdh);
    free(fdh->map);
    free(fdh->occ);
    free(fdh);
    fdh = NULL;
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

