#ifndef ASCLIB_SYNC_H_
#define ASCLIB_SYNC_H_

#include "core.h"

typedef struct _ASCLIB_One_Shot_Channel oneshot_t;

task_handle asc_sleep(uint64_t ms);

#endif // DEMO_ASCLIB_SYNC_H_