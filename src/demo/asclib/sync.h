#ifndef ASCLIB_SYNC_H_
#define ASCLIB_SYNC_H_

#include "core.h"

typedef struct _ASCLIB_One_Shot_Channel oneshot_t;
typedef struct _ASCLIB_mpsc_Channel mpsc_t;
typedef struct _ASCLIB_spsc_Channel spsc_t;

task_handle_t asc_sleep(uint64_t ms);

#endif // DEMO_ASCLIB_SYNC_H_