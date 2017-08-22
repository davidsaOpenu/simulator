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


// old monitor data
int servSock;
int clientSock = 0;
int g_init_log_server = 0;
FILE *monitor = NULL;


// new logging server data
/**
 * The size of each logger, in bytes (currently ~16Mb)
 */
#define LOGGER_SIZE (1 << 24)
/**
 * The loggers themselves
 */
Logger **loggers;
/**
 * The real time log analyzers
 */
RTLogAnalyzer **rt_log_analyzers;
/**
 * The real time log analyzers threads
 */
pthread_t *rt_log_analyzers_threads;
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


void INIT_LOG_MANAGER(void)
{
    int i;

    // handle old monitor
#ifdef MONITOR_ON
	if(g_init_log_server == 0){
		if ((monitor = popen("./ssd_monitor", "r")) == NULL)
			PERR("popen failed: %s\n", strerror(errno));
		THREAD_SERVER();

		g_init_log_server = 1;
	}
#endif

	// handle new logging server
#ifdef LOGGING_SERVER_ON
	// allocate memory
    loggers = (Logger**) malloc(sizeof(Logger*) * FLASH_NB);
    if (loggers == NULL)
        PERR("Couldn't allocate memory for the loggers: %s\n", strerror(errno));
    rt_log_analyzers = (RTLogAnalyzer**) malloc(sizeof(RTLogAnalyzer*) * FLASH_NB);
    if (rt_log_analyzers == NULL)
        PERR("Couldn't allocate memory for the real time analyzers: %s\n", strerror(errno));
    rt_log_analyzers_threads = (pthread_t*) malloc(sizeof(pthread_t) * FLASH_NB);
    if (rt_log_analyzers_threads == NULL)
        PERR("Couldn't allocate memory for the real time analyzers' threads: %s\n", strerror(errno));

	// init structures
	for (i = 0; i < FLASH_NB; i++) {
	    loggers[i] = logger_init(LOGGER_SIZE);
	    if (loggers[i] == NULL)
	        PERR("Couldn't create the logger: %s\n", strerror(errno));
	    rt_log_analyzers[i] = rt_log_analyzer_init(loggers[i]);
	    if (rt_log_analyzers[i] == NULL)
	        PERR("Couldn't create the real time analyzer: %s\n", strerror(errno));
	}
	log_manager = log_manager_init();
	if (log_manager == NULL)
	    PERR("Couldn't create the log manager: %s\n", strerror(errno));
	log_server_init();

	// connect the logging mechanism
	for (i = 0; i < FLASH_NB; i++) {
	    log_manager_add_analyzer(log_manager, rt_log_analyzers[i]);
    }
	log_manager_subscribe(log_manager, log_server_update, NULL);

	// run the threads
	pthread_create(&log_server_thread, NULL, log_server_run, NULL);
	pthread_create(&log_manager_thread, NULL, log_manager_run, log_manager);
	for (i = 0; i < FLASH_NB; i++) {
	    pthread_create(&(rt_log_analyzers_threads[i]), NULL, rt_log_analyzer_run, rt_log_analyzers[i]);
	}

    PINFO("Log server opened\n");
    PINFO("Browse to http://127.0.0.1:%d/ to see the current statistics\n", LOG_SERVER_PORT);
#endif
}
void TERM_LOG_MANAGER(void)
{
    int i;

    // handle old monitor
#ifdef MONITOR_ON
	close(servSock);
	close(clientSock);
	pclose(monitor);
#endif

	// handle new logging server
#ifdef LOGGING_SERVER_ON
	// term log analyzers and loggers
	for (i = 0; i < FLASH_NB; i++) {
	    if (pthread_kill(rt_log_analyzers_threads[i], SIGTERM))
	        PERR("Couldn't kill real time analyzer thread: %s\n", strerror(errno));
	    pthread_cancel(rt_log_analyzers_threads[i]);
	    pthread_join(rt_log_analyzers_threads[i], NULL);
	    rt_log_analyzer_free(rt_log_analyzers[i], 1);
	}

	// term log manager
    if (pthread_kill(log_manager_thread, SIGTERM))
        PERR("Couldn't kill log manager thread: %s\n", strerror(errno));
    pthread_cancel(log_manager_thread);
    pthread_join(log_manager_thread, NULL);
    log_manager_free(log_manager);

	// term log server
	if (pthread_kill(log_server_thread, SIGTERM))
	    PERR("Couldn't kill log server thread: %s\n", strerror(errno));
	pthread_cancel(log_server_thread);
	pthread_join(log_server_thread, NULL);
	log_server_free();
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

