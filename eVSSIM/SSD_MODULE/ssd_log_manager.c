// Copyright(c)2013
//
// Hanyang University, Seoul, Korea
// Embedded Software Systems Lab. All right reserved

#include "ssd_log_manager.h"
#include "common.h"

#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <memory.h>
#include <errno.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdarg.h>
#include <signal.h>

#ifdef VSSIM_NEXTGEN_BUILD_SYSTEM
    #define MONITOR_EXECUTABLE_PATH ("./vssim/simulator/ssd_monitor")
#else
    #define MONITOR_EXECUTABLE_PATH ("./ssd_monitor")
#endif

// old monitor data
int servSock;
int clientSock = 0;
int g_init_log_server = 0;
FILE *monitor = NULL;

// new logging server data
/**
 * The loggers' + real time analyzers' data
 */
LoggerAnalyzerStorage* analyzers_storage;
/**
 * The log manager struct
 */
LogManager* log_manager;
/**
 * The log manager thread
 */
pthread_t log_manager_thread;
/**
 * The log server thread
 */
pthread_t log_server_thread;

void reset_analyzers(void) {
    int i;
    for (i = 0; i < FLASH_NB; i++)
        analyzers_storage[i].rt_log_analyzer->reset_flag = 1;
}

void INIT_LOG_MANAGER(void)
{
    // handle old monitor
#ifdef MONITOR_ON
	if(g_init_log_server == 0){
		if ((monitor = popen(MONITOR_EXECUTABLE_PATH, "r")) == NULL)
			PERR("popen failed: %s\n", strerror(errno));
		THREAD_SERVER();

        g_init_log_server = 1;
    }
#endif

    // handle new logging server
#ifdef LOGGING_SERVER_ON
    int i;

    // allocate memory
    analyzers_storage = (LoggerAnalyzerStorage*) malloc(sizeof(LoggerAnalyzerStorage) * FLASH_NB);
    if (analyzers_storage == NULL)
        PERR("Couldn't allocate memory for the loggers and real time analyzers: %s\n",
                strerror(errno));

    // init structures
    for (i = 0; i < FLASH_NB; i++) {
        analyzers_storage[i].logger = logger_init(DEFUALT_LOGGER_POOL_SIZE);
        if (analyzers_storage[i].logger == NULL)
            PERR("Couldn't create the logger: %s\n", strerror(errno));
        analyzers_storage[i].rt_log_analyzer = rt_log_analyzer_init(analyzers_storage[i].logger);
        if (analyzers_storage[i].rt_log_analyzer == NULL)
            PERR("Couldn't create the real time analyzer: %s\n", strerror(errno));
        analyzers_storage[i].offline_log_analyzer = offline_log_analyzer_init(analyzers_storage[i].logger);
        if (analyzers_storage[i].offline_log_analyzer == NULL)
            PERR("Couldn't create the offline log analyzer: %s\n", strerror(errno));
    }

    log_manager = log_manager_init();
    if (log_manager == NULL)
        PERR("Couldn't create the log manager: %s\n", strerror(errno));
    log_server_init();

    // connect the logging mechanism
    for (i = 0; i < FLASH_NB; i++) {
        log_manager_add_analyzer(log_manager, analyzers_storage[i].rt_log_analyzer);
    }
    log_manager_subscribe(log_manager, log_server_update, NULL);
    log_server_on_reset(reset_analyzers);

    // run the threads
    pthread_create(&log_server_thread, NULL, log_server_run, NULL);
    pthread_create(&log_manager_thread, NULL, log_manager_run, log_manager);
    for (i = 0; i < FLASH_NB; i++) {
        // run rt log analyzer
        pthread_create(&(analyzers_storage[i].rt_log_analyzer_thread), NULL, rt_log_analyzer_run,
                analyzers_storage[i].rt_log_analyzer);
        // run offline log analyzer
        pthread_create(&(analyzers_storage[i].offline_log_analyzer_thread), NULL,
                offline_log_analyzer_run, analyzers_storage[i].offline_log_analyzer);
    }

    PINFO("Log server opened\n");
    PINFO("Browse to http://127.0.0.1:%d/ to see the current statistics\n", LOG_SERVER_PORT);
#endif
}

void TERM_LOG_MANAGER(void)
{
    // handle old monitor
#ifdef MONITOR_ON
    close(servSock);
    close(clientSock);
    pclose(monitor);
#endif

    // handle new logging server
#ifdef LOGGING_SERVER_ON
    int i;

    // alert the different threads to stop
    for (i = 0; i < FLASH_NB; i++)
    {
        analyzers_storage[i].rt_log_analyzer->exit_loop_flag = 1;
        analyzers_storage[i].offline_log_analyzer->exit_loop_flag = 1;
    }
    log_manager->exit_loop_flag = 1;
    log_server.exit_loop_flag = 1;

    // wait for the different threads to stop, and clear their data
    for (i = 0; i < FLASH_NB; i++) {
        pthread_join(analyzers_storage[i].rt_log_analyzer_thread, NULL);
        pthread_join(analyzers_storage[i].offline_log_analyzer_thread, NULL);
        rt_log_analyzer_free(analyzers_storage[i].rt_log_analyzer, 1);
        offline_log_analyzer_free(analyzers_storage[i].offline_log_analyzer);
    }
    pthread_join(log_manager_thread, NULL);
    log_manager_free(log_manager);
    pthread_join(log_server_thread, NULL);
    log_server_free();

    // free allocated memory
    free(analyzers_storage);
#endif
}

void WRITE_LOG(const char *fmt, ...)
{
#ifdef MONITOR_ON
    if (clientSock == 0)
        RERR(, "write log is failed\n");

    char szLog[1024];
    va_list argp;
    va_start(argp, fmt);
    vsprintf(szLog, fmt, argp);
    send(clientSock, szLog, strlen(szLog), 0);
    send(clientSock, "\n", 1, MSG_DONTWAIT);
    va_end(argp);
#endif
}

void THREAD_SERVER(void)
{
#ifdef MONITOR_ON
    PDBG_MNT("Start\n");

    if ((servSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
        RDBG_MNT(, "socket failed: %s\n", strerror(errno));
    int flags = fcntl(servSock, F_GETFL, 0);

    fcntl(servSock, F_SETFL, flags | O_APPEND);
    int option = 1;
    if (setsockopt(servSock, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option)) < 0)
        RDBG_MNT(, "setsockopt SO_REUSEADDR failed: %s\n", strerror(errno));

    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0x00, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddr.sin_port = htons(9999);

    if (bind(servSock, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
        RDBG_MNT(, "bind failed: %s\n", strerror(errno));

    if (listen(servSock, 100) < 0)
        RDBG_MNT(, "listen failed: %s\n", strerror(errno));

    PDBG_MNT("Wait for client....[%d]\n", servSock);

    if ((clientSock = accept(servSock, NULL, NULL)) < 0)
        RDBG_MNT(, "accept failed: %s\n", strerror(errno));
    PDBG_MNT("Connected![%d]\n", clientSock);
#endif
}

void THREAD_CLIENT(void *arg)
{
#ifdef MONITOR_ON
    int sock = *(int*)arg;
    PDBG_MNT("ClientSock[%d]\n", sock);
    send(sock, "test\n", 5, 0);
#endif
}

Logger_Pool* GET_LOGGER(unsigned int flash_number) {
#ifdef LOGGING_SERVER_ON
    if (flash_number < FLASH_NB)
        return analyzers_storage[flash_number].logger;
#endif
    return NULL;
}
