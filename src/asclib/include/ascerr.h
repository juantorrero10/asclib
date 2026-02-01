#ifndef ERR_H_
#define ERR_H_

#include <stdio.h>
#include <stdint.h>
#include <defs/TCB.h>

#define _ASCERR_ERR_START 100
#define _ASCERR_WARN_START 200


typedef enum {
    OK,
    PANIC_NO_STACK,
    PANIC_UNAWAITED_DESTROY,
    PANIC_STACK_OVERFLOW_GUARDPAGE,
    PANIC_STACK_OVERFLOW_RSP_BOUND,
    PANIC_CONTEXT_VIOLATION,
    PANIC_AWAIT_SELF,
    PANIC_AWAIT_INVALID_ID,
    PANIC_EXPECT_NULL,
    PANIC_GENERIC_ASSERTION,
    ERR_TASK_OUTLIVED_FATHER = _ASCERR_ERR_START,
    ERR_TASK_OUTLIVED_DESTROYER,
    WARN_AWAITING_DETACHED_TASK = _ASCERR_WARN_START,
    WARN_TRACKING_UNMARKED_OBJECT,
    WARN_TASK_ALREADY_FINISHED,
    WARN_RETURNING_NULL,
    WARN_STALE_HANDLE,
    WARN_KEY_READONLY
} asc_err_t;

typedef enum {
    IO_OK,
    IO_PANIC_ILLEGAL_FD,
    IO_PANIC_LOCKED_FD,
    IO_ERROR_FILE_NOT_FOUND = _ASCERR_ERR_START,
    IO_WARN_NETWORK_TIMEOUT = _ASCERR_WARN_START,
} io_err_t;

typedef enum {
    CHAN_OK = 0,
    CHAN_PANIC_UNAUTHORIZED,     // Requester, owner
    CHAN_PANIC_STATE_CORRUPTION, // Task_id, expected status, gotten status
    CHAN_ERROR_RX_INVALID = _ASCERR_ERR_START, // NO args
    CHAN_ERROR_RX_LOCKED,       // Requester
    CHAN_ERROR_TX_LOCKED,
    CHAN_ERROR_ALREADY_USED,
    CHAN_ERROR_DEAD_CHANNEL,    // NO args 
    CHAN_ERROR_NULL_PTR,        // NO args
    CHAN_WARN_DOUBLE_DROP = _ASCERR_WARN_START, // No args
    CHAN_WARN_RETURN_NULL,      // NO args
} chan_err_t;


typedef union {
    uint64_t u64;
    int64_t i64;
    double f64;
    void* ptr;
    int32_t i32;
    task_status ts;
} any_t;

#define RT_ERR(msg, function, ...) fprintf(stderr, "\033[4m\033[91masclib runtime error at %s:\033[0m "msg"\n", function, ##__VA_ARGS__)
#define RT_ERR_FLESS(msg, ...) fprintf(stderr, "\033[4m\033[91masclib runtime error:\033[0m "msg"\n", ##__VA_ARGS__)
#define RT_WRN(msg, ...) fprintf(stderr, "\033[4m\033[93masclib runtime warning:\033[0m "msg"\n", ##__VA_ARGS__)
#define PK_MSG(loc, msg, ...) fprintf(stderr, "\033[1m\033[97m%s panicked:\033[0m %s\n", loc, msg, ##__VA_ARGS__)
#define EXPL(msg, ...) fprintf(stderr, "\033[1m\033[97m ( + ) \033[0m --> "msg"\n", ##__VA_ARGS__)
void asc_error(asc_err_t err, const char* function, any_t number);
void asc_io_error(io_err_t ioerr, const char* function, any_t value, any_t value2);
void asc_chan_error(chan_err_t cerr, const char* function, any_t value, any_t value2, any_t value3);
#endif // ERR_H_