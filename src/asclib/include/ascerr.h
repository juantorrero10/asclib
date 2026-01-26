#ifndef ERR_H_
#define ERR_H_

#include <stdio.h>

typedef enum {
    PANICK_UNAWAITED_DESTROY,
    ERR_TASK_OUTLIVED_FATHER,
    ERR_TASK_OUTLIVED_DESTROYER,
    WARN_AWAITING_DETACHED_TASK,
} asclib_err_t;

#define RT_ERR(msg, function, ...) printf("\033[4m\033[91masclib runtime error at %s:\033[0m "msg"\n", function, ##__VA_ARGS__)
#define RT_WRN(msg, ...) printf("\033[4m\033[93masclib runtime warning:\033[0m "msg"\n", ##__VA_ARGS__)
#define PK_MSG(loc, msg, ...) printf("\033[1m\033[97m%s panicked:\033[0m %s\n", loc, msg, ##__VA_ARGS__)
#define EXPL(msg, ...) printf("\033[1m\033[97m ( + ) \033[0m --> "msg"\n", ##__VA_ARGS__)
void asc_error(asclib_err_t err, const char* function, int number);
#endif // ERR_H_