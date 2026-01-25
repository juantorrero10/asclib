#ifndef LOG_H_


#ifdef __DEBUG
#define NL() printf("\r\n");
#define SEP() printf("----------------------------------------\r\n");
#define LOG_SUCCESS(MSG, ...) printf("[\x1b[35mS\x1b[0m] \x1b[35mSuccess!!\x1b[0m: "    MSG"\r\n", ##__VA_ARGS__)

#define LOG(MSG, ...) printf(MSG, ##__VA_ARGS__)
#define OKAY(MSG, ...) printf("\x1b[32m[+]\x1b[0m "          MSG "\r\n", ##__VA_ARGS__)
#define INFO(MSG, ...) printf("\x1b[34m[*]\x1b[0m "          MSG "\r\n", ##__VA_ARGS__)
#define WARN(MSG, ...) printf("\x1b[33m[-]\x1b[0m "          MSG "\r\n", ##__VA_ARGS__)
#define ERROR(MSG, ...)     printf("\x1b[31m[!]\x1b[0m "          MSG "\r\n", ##__VA_ARGS__)
#define INDENTED(MSG, ...)  printf("     "MSG "\r\n", ##__VA_ARGS__)
#define DUMP(MSG, ...) printf("\x1b[32mD:\x1b[0m "      MSG "\r\n", ##__VA_ARGS__)
#define ERROR_INFO(FUNCTION_NAME, errorcode)                                   \
do {                                                             \
fprintf(stderr,                                              \
"\x1b[31m[>]\x1b[0m [" FUNCTION_NAME "] failed, error: 0x%lx\r\n"     \
"   |------->\x1b[31m%s:%d\x1b[0m \r\n", errorcode, __FILE__, __LINE__);  \
} while (0)

#else
#define NL() (void)0
#define SEP() (void)0
#define LOG_SUCCESS(MSG, ...) (void)0
#define LOG(MSG, ...) (void)0
#define OKAY(MSG, ...) (void)0
#define WARN(MSG, ...) (void)0
#define INFO(MSG, ...) (void)0
#define PRINT_ERROR(FUNCTION_NAME, errorcode) (void)0
#define INDENTED(MSG, ...) (void)0
#define ERROR(MSG, ...) (void)0
#define DUMP(MSG, ...) (void)0
#endif //DO_LOG

#endif // LOG_H_