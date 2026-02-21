#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <assert.h>
#include <sys/inotify.h>
#include <dirent.h>
#include <sys/stat.h>
#include <limits.h>
#include <errno.h>
#include <string.h>


#include <dispatcher.h>
#include <ascerr.h>
#include <collections.h>
#include <user_lib.h>
#include <task_mem.h>
#include <runtime.h>

extern TLS g_arrlist_t* generation_map;


#define ENABLE_GUARD_PAGE 1

TLS timer_heap_t* timers = NULL;

uint64_t get_time_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (ts.tv_nsec / 1000000);
}

// Set wake up timer and return to dispatcher
void* __internal_async_sleep(void* __arg) {
    uint64_t ms = ((uint64_t)__arg);


    assert(timers);

    worker_t* w = tl_worker;
    TCB* t = w->tasks.arr[w->tasks.current];

    t->wait_for.timestamp = get_time_ms() + ms;
    t->status = WAITING;
    t->w_reason = WAIT_TIMER;

    heap_push(timers, GET_FULL_HANDLE(), t->wait_for.timestamp);

    __task_yield_impl();

    return NULL;
}

/**
 * @brief create non-blocking sleep task
 * @param ms Milliseconds to-sleep
 * @note use await() to block current task until time its over
 */
task_handle_t asc_sleep(uint64_t ms) {
    if (ms == 0) return -1;

    task_entry_t te = {
        .fptr = __internal_async_sleep,
        .value = (void*)ms,
        .arg_size = sizeof(void*),
        .n_pages = 0,
        .arg_malloced = 0
    };
    
    // Create concurrent task, without page guard and tiny stack.
    return __task_create_ex(te, 4096, ENABLE_GUARD_PAGE);
    
}



void try_wake_up(TCB* candidate) {

    if (candidate->w_reason == WAIT_TIMER && get_time_ms() >= candidate->wait_for.timestamp) {
        candidate->wait_for.timestamp = 0;
        candidate->w_reason = WAIT_NONE;
        candidate->status = READY;
    }
}



#define MAX_RECURSION_LVL 10

static inline int __is_dir(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return S_ISDIR(st.st_mode);
}

static void __watch_file(int ifd, u64_arrlist_t* wds, char* path, int recursive, int rec_lvl) {
    
    u64al_append(wds, (uint64_t)inotify_add_watch(ifd, path, IN_CLOSE_WRITE | IN_MOVED_TO));   
    if (recursive && rec_lvl < MAX_RECURSION_LVL) {
        DIR* d = opendir(path);
        if (!d) return;
        struct dirent* entry;
        while ((entry = readdir(d)) != NULL) {
            // skill ./. and ./..
            if (!strcmp(".", entry->d_name) || !strcmp("..", entry->d_name)) continue;

            char _path[PATH_MAX];
            sprintf(_path, "%s/%s", path, entry->d_name);
            if (__is_dir(_path)) {
                __watch_file(ifd, wds, _path, 1, rec_lvl + 1);
            }
        }
        closedir(d);
        
    }
}



async(__internal_inotify, char** _files; char** ofc; int r) {
    char** files = arg(_files);
    char** ofc = arg(ofc); // Optional output file that changed
    int recursive = arg(r);
    worker_t* w = tl_worker;


    if (!files || !*files) task_return(int, -1);
    int ifd = inotify_init1(IN_NONBLOCK);
    size_t amnt = 0;
    char* ptr = (char*)files;
    int ret = 0;
    // Get the size
    while(*files) {
        files++;
        amnt++;
    }
    files = (char**)ptr;

    // Add to watch
    u64_arrlist_t* wds = u64al_alloc(amnt);
    for (size_t i = 0; i < amnt; i++)
    {
        __watch_file(ifd, wds, files[i], recursive, 0);
    }
    

    // Read loop
    char buffer[4096] __attribute__ ((aligned(__alignof__(struct inotify_event))));
    __claim_fd(ifd, w->tasks.current, EPOLLIN);
    for(;;) {
        ssize_t sz = read(ifd, buffer, 4096);
        
        // File changed.
        if (sz >= 0) {
            __release_fd(ifd);
            if (!ofc) goto __exit;
            ptr = buffer;
            while (ptr < buffer + sz) {
                struct inotify_event* event = (struct inotify_event*)ptr;
                for (size_t i = 0; i < wds->size; i++)
                {
                    if (event->wd == (int)wds->data[i]) {
                        *ofc = files[i];
                        goto __exit;
                    }
                    ptr += sizeof(struct inotify_event) + event->len;
                }
            }
        }

        // Nothing yet.
        else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            __task_wait_on_fd(ifd);
            continue;
        }

        // Error
        else {
            __release_fd(ifd);
            ret = -1;
            goto __exit;
        }
    }
    
__exit:
    u64al_dealloc(&wds);
    close(ifd);
    end_return(int, ret);
}


task_handle_t asc_inotify(char* files[], char** out_file_changed, int __recursive) {
    worker_t* w = tl_worker;
    ASSERT_CONTEXT(w, "SYNC::inotify()");
    return task_spawn_ex(__internal_inotify, 3 + (MAX_RECURSION_LVL * (__recursive != 0)) * 4096, 1, ._files = files, .ofc = out_file_changed, .r = __recursive);
}

