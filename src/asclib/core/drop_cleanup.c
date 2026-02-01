

#include <collections.h>
#include <ascerr.h>
#include <channels.h>
#include <dispatcher.h>
#include <task_control.h>
#include <task_mem.h>

#define __DEBUG

#include <log.h>


typedef struct {
    void(*drop_)(void*);
    void* obj;
} object_drop_autocleanup_t;

#define INITIAL_CAP (size_t)2


void __drop_cleanup_register(object_drop_autocleanup_t oda) {
    worker_t* w = tl_worker;
    ASSERT_CONTEXT(w, "_drop_cleanup_register()");

    size_t curr = w->tasks.current;
    TCB* t = w->tasks.arr[curr];

    if (!t->cleanup) {
        t->cleanup = gal_alloc(INITIAL_CAP, sizeof(object_drop_autocleanup_t));
    }

    gal_append(t->cleanup, &oda);

    return;

}

void __drop_designated_objects(task_handle_t th) {
    worker_t* w = tl_worker;

    th = LOWER_32(th);

    if (!TCB_AT(th)) {
        return;
    }

    TCB* t = w->tasks.arr[th];
    object_drop_autocleanup_t oda = { 0 };
    while(t->cleanup->size > 0)
    {
        if (gal_pop(t->cleanup, &oda)) {
            if (oda.drop_)oda.drop_(oda.obj);
        }

    }

}


