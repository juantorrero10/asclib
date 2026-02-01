#define _POSIX_SOURCE
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>

#include <ascerr.h>
#include <dispatcher.h>
#include <collections.h>

extern TLS uu_hash_map* fdh;

static ssize_t get_owner(int fd) {
    assert(fdh);
    return hm_get_v(fdh, (size_t)fd);
}


void panic_linux(const char* msg, const char* function) {
    PK_MSG(function, msg);
    fflush(stderr);
    abort();
}



void asc_error(asclib_err_t err, const char* function, any_t value) {

    switch (err)
    {
    case ERR_TASK_OUTLIVED_FATHER:
        RT_ERR("{%ld} Undetached tasks outlived father's lifetime. May cause memory leaks, \033[4mforgot to \033[1m\033[97mawait()?\033[0m", function, value.i64);
        EXPL("If this is intended behaviour \033[4mcall \033[1m\033[97mtask_detach()\033[0m anywhere inside the task before the return statement.");
        break;

    case ERR_TASK_OUTLIVED_DESTROYER:
        RT_ERR("Tried to destroy worker with detached tasks still pending. ", function);
        EXPL("If you are going to use \033[1m\033[97mtask_detach()\033[0m \033[4mcall \033[1m\033[97mworker_await_all()\033[0m before destroying.");
        EXPL("Possible memory leak if task doesn't end before the main program does.");
        break;

    case PANIC_UNAWAITED_DESTROY:
        RT_ERR_FLESS("Tried to destroy worker with yet to be completed tasks, \033[4muse \033[1m\033[97mworker_await().\033[0m");
        panic_linux("This error may cause undefined behaviour.", function);
        break;

    case PANIC_CONTEXT_VIOLATION:
        RT_ERR_FLESS("Illegal call to \033[1m\033[97m%s\033[0m from outside a task context.", function);
        EXPL("The runtime can only \033[4myield\033[0m, \033[4mawait()\033[0m or \033[4mdetach\033[0m inside a \033[1m\033[97masync(...)\033[0m block.");
        panic_linux("Execution cannot continue outside of a managing dispatcher", function);
        break;

    case PANIC_STACK_OVERFLOW_GUARDPAGE:
        RT_ERR_FLESS("Triggered guard page 'trap' at the top of the stack, illegal access @ \033[1m\033[97m%p\033[0m.", *(void**)&value);
        panic_linux("\033[4mStack overflow\033[0m, a bigger stack can be allocated with \033[1m\033[97mtask_new_ex(...)\033[0m or \033[1m\033[97mtask_entry_ex(...)\033[0m.", function);
        break;

    case PANIC_AWAIT_SELF:
        RT_ERR("Task<%ld> tried to await to itself.", function, value.i64);
        EXPL("Likely caused by calling with raw ID which is highly discouraged.");
        panic_linux("Execution would have reached a \033[4mdeadlock\033[0m", function);
        break;

    case PANIC_AWAIT_INVALID_ID:
        RT_ERR("Task handle provided {0x%lx} is an invalid handle. \033[4mValue was \033[1m\033[97minvalid\033[0m\033[4m or \033[1m\033[97mout of bounds\033[0m.", function, value.u64);
        panic_linux("Task would have \033[1m\033[97mblocked indefinitely.\033[0m", function);
        break;

    case PANIC_STACK_OVERFLOW_RSP_BOUND:
        RT_ERR("Detected stack pointer at illegal location, exceding stack boundaries. \033[1m\033[97mrsp @ %p\033[0m.", function, value.ptr);
        panic_linux("\033[4mStack overflow\033[0m, a bigger stack can be allocated with \033[1m\033[97mtask_new_ex(...)\033[0m or \033[1m\033[97mtask_entry_ex(...)\033[0m.", function);
        break;

    case PANIC_EXPECT_NULL:
        RT_ERR("Task<%zu> returned nothing, yet a return value is being expected.", function, value.u64);
        panic_linux("Tried to dereference a NULL pointer, \033[4meither return something with \033[1m\033[97mtask_return( ... )\033[0m\033[4m or don't use \033[1m\033[97mawait_ret()\033[0m", function);
        break;

    case PANIC_GENERIC_ASSERTION:
        RT_ERR("Assertion failed: '%s'", function, (char*)value.ptr);
        panic_linux("This operation cannot complete while the assertion fails.", function);
        break;

    case WARN_AWAITING_DETACHED_TASK:
        RT_WRN("The task<%ld> marked as 'detached' is being awaited. Don't \033[1m\033[97mawait()\033[0m it or don't use \033[1m\033[97mdetach\033[0m", value.i64);
        EXPL("In this situation the await() call is ignored.");
        break;

    case WARN_TRACKING_UNMARKED_OBJECT:
        RT_WRN("Tried tracking the closure of object of type \033[1m\033[97m<%s>\033[0m but it was not marked when created.", (char*)value.ptr);
        EXPL("Revise the parameters of the object's creation");
        break;

    case WARN_RETURNING_NULL:
        RT_WRN("Operation '%s' is returning a NULL pointer when a valid value is potencially being expected.", (char*)value.ptr);
        EXPL("Could produce a \033[1m\033[91mSIGSEGV\033[0m if not handled gracefully.");
        break;

    case WARN_STALE_HANDLE:
        RT_WRN("Task handled provided {0x%lx} \033[4mhas expired\033[0m. The task referenced by this handle has already been cleaned up or replaced. ", value.u64);
        EXPL("This is \033[1m\033[91munsafe behaviour\033[0m and it could leak to crashes or \033[1m\033[97mresource leaks\033[0m through buffers and return values.");
        EXPL("Awaiting call was \033[1m\033[97mignored\033[0m. It is strongly recommended to rewrite the logic and order of operations to prevent this. ");
        break;
    default:
        break;
    }
}


void asc_io_error(asclib_err_io_t ioerr, const char* function, any_t value, any_t value2) {
    (void)value2;
    switch (ioerr)
    {
    case IO_ERROR_FILE_NOT_FOUND:
        RT_ERR("Could not open file: '%s'", function, (char*)value.ptr);
        break;

    case IO_PANIC_ILLEGAL_FD:
        RT_ERR("Bad file descriptor: {%ld}", function, value.i64);
        panic_linux("Operations on a corrupted/invalid fd could lead to \033[4mundefined behaviour\033[0m.", function);
        break;

    case IO_PANIC_LOCKED_FD:
        ssize_t owner = get_owner(value.i64);
        RT_ERR("File descriptor {%ld} was already being used by Task<%zd> and Task<%zd> tried to claim it", function, value.i64, owner, value2.i64);
        panic_linux("The same file descriptor cannot be used at the same time by two tasks", function);
        break;
    default:
        break;
    }
}

void asc_chan_error(asclib_err_channels_t cerr, const char* function, any_t value, any_t value2, any_t value3) {
    switch (cerr) {
        /* --- PANICS (Execution stops) --- */
        
        case CHAN_PANIC_UNAUTHORIZED:
            // value: Requester ID, value2: Owner ID
            RT_ERR("Ownership Violation: Task<0x%lx> tried to access channel owned by Task<0x%lx>.", function, value.i64, value2.i64);
            EXPL("MPSC channels are \033[1mStrictly Single Consumer\033[0m.");
            EXPL("Ensure you called \033[1m\033[97mmpsc_take_rx()\033[0m or that you aren't sharing the RX end.");
            panic_linux("Illegal cross-task channel access.", function);
            break;

        case CHAN_PANIC_STATE_CORRUPTION:
            // value: Task_id, value2: expected status, value3: gotten status
            RT_ERR("State Machine Corruption: Task<%ld> in waiter list should be [%d], but is [%d].", 
                   function, value.i64, value2.ts, value3.ts);
            EXPL("This usually indicates a \033[4mDouble-Wakeup\033[0m or an internal runtime race condition.");
            panic_linux("Channel internal state is no longer deterministic.", function);
            break;

        /* --- ERRORS (User code logic failure) --- */

        case CHAN_ERROR_RX_INVALID:
            RT_ERR("Operation failed: The RX end of the channel \033[4mis invalid or its already closed.\033[0m", function);
            EXPL("Check the lifecycle of the channel; the consumer may have already called \033[1m\033[97mmpsc_drop_rx()\033[0m.");
            break;

        case CHAN_ERROR_RX_LOCKED:
            // value: Requester ID
            RT_ERR("Task<%ld> tried to take the rx end of a channel but it is owned by another task.", function, value.i64);
            EXPL("MPSC channels only support \033[1mone\033[0m waiter on the RX end at a time.");
            break;

        case CHAN_ERROR_DEAD_CHANNEL:
            RT_ERR("Tried to use a dead channel.", function);
            EXPL("Caught this error due to tracking but it would have produce a \033[1m\033[91mSIGSEGV\033[0m otherwise.");
            break;

        case CHAN_ERROR_ALREADY_USED:
            RT_ERR("Tried to reuse an unrecyclable channel-end.", function);
            break;

        case CHAN_ERROR_NULL_PTR:
            RT_ERR("Illegal NULL pointer passed to channel operation.", function);
            break;

        /* --- WARNINGS (Subtle issues / Soft failures) --- */

        case CHAN_WARN_DOUBLE_DROP:
            RT_WRN("Object was dropped more than once.");
            EXPL("Harmless if object is being tracked, but indicates \033[4mredundant logic\033[0m in a task.");
            break;

        case CHAN_WARN_RETURN_NULL:
            RT_WRN("Returning a \033[1m\033[97mNULL\033[0m pointer due to bad parameters.");
            EXPL("Be aware of a possible \033[1m\033[91mSIGSEGV\033[0m when using this pointer.");
            break;

        default:
            RT_WRN("Unknown channel error code: %d", cerr);
            break;
    }
}
