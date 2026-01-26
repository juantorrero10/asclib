#define _POSIX_SOURCE
#include <signal.h>
#include <unistd.h>

#include <ascerr.h>


void panick_linux(const char* msg, const char* function) {
    PK_MSG(function, msg);
    kill(getpid(), SIGABRT);
}




void asc_error(asclib_err_t err, const char* function, int number) {

    switch (err)
    {
    case ERR_TASK_OUTLIVED_FATHER:
        RT_ERR("{%d} Undetached tasks outlived father's lifetime. May cause memory leaks, \033[4mforgot to \033[1m\033[97mawait()?\033[0m", function, number);
        EXPL("If this is intended behaviour \033[4mcall \033[1m\033[97mtask_detach()\033[0m anywhere inside the task before the return statement.");
        break;

    case ERR_TASK_OUTLIVED_DESTROYER:
        RT_ERR("Tried to destroy worker with detached tasks still pending. ", function);
        EXPL("If you are going to use \033[1m\033[97mtask_detach()\033[0m \033[4mcall \033[1m\033[97mworker_await_all()\033[0m before destroying.");
        EXPL("Possible memory leak if task doesn't end before the main program does.");
        break;

    case PANICK_UNAWAITED_DESTROY:
        RT_ERR("Tried to destroy worker with yet to be completed tasks, \033[4muse \033[1m\033[97mworker_await().\033[0m", function);
        panick_linux("This error may cause undefined behaviour.", function);
        break;
    
    case WARN_AWAITING_DETACHED_TASK:
        RT_WRN("The task<%d> marked as 'detached' is being awaited. Remove the \033[1m\033[97mawait()\033[0m call or don't use \033[1m\033[97mtask_detach()\033[0m", number);
        EXPL("In this situation the await() call is ignored.");
        break;
    default:
        break;
    }
}
