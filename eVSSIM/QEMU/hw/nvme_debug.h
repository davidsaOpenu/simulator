#define APPNAME         "qnvme"
#define LEVEL           APPNAME
#define DEBUG

#define CHOP_FILE(str) (strstr(str, "nvmeqemu") == 0 ? str : strstr(str, "nvmeqemu") + 9)

#define LOG_NORM(fmt, ...)    \
    printf("%s: " fmt "\n", LEVEL, ## __VA_ARGS__)
#define LOG_ERR(fmt, ...)    \
    printf("%s-ERR:%s:%d: " fmt "\n", LEVEL, CHOP_FILE(__FILE__), \
        __LINE__, ## __VA_ARGS__)
#ifdef DEBUG
#define LOG_DBG(fmt, ...)    \
    printf("DBG|:%s:%d: " fmt "\n", CHOP_FILE(__FILE__), \
        __LINE__, ## __VA_ARGS__)
#else
#define LOG_DBG(fmt, ...)
#endif
