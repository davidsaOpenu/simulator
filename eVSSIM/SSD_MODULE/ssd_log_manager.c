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


/**
 * The data allocated for each logger and real time analyzer/offline analyzer
 */
typedef struct {
    /**
     * The logger itself
     */
    Logger_Pool* logger;
    /**
     * The real time analyzer of the logger
     */
    RTLogAnalyzer* rt_log_analyzer;
    /**
     * The offline analyzer of the logger pool
     */
    OfflineLogAnalyzer* offline_log_analyzer;
    /**
     * The thread of the real time analyzer
     */
    pthread_t rt_log_analyzer_thread;
    /**
     * The thread of the offline log analyzer
     */
    pthread_t offline_log_analyzer_thread;
} LoggerAnalyzerStorage;

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
    uint32_t i;
    for (i = 0; i < devices[current_device_index].flash_nb; i++)
        analyzers_storage[i].rt_log_analyzer->reset_flag = 1;
}

void INIT_LOG_MANAGER(void)
{
    uint32_t i;

    // allocate memory
    analyzers_storage = (LoggerAnalyzerStorage*) malloc(sizeof(LoggerAnalyzerStorage) * devices[current_device_index].flash_nb);
    if (analyzers_storage == NULL)
        PERR("Couldn't allocate memory for the loggers and real time analyzers: %s\n",
                strerror(errno));

    // init structures
    for (i = 0; i < devices[current_device_index].flash_nb; i++) {
        analyzers_storage[i].logger = logger_init(DEFUALT_LOGGER_POOL_SIZE);
        if (analyzers_storage[i].logger == NULL)
            PERR("Couldn't create the logger: %s\n", strerror(errno));
        analyzers_storage[i].rt_log_analyzer = rt_log_analyzer_init(analyzers_storage[i].logger, i);
        if (analyzers_storage[i].rt_log_analyzer == NULL)
            PERR("Couldn't create the real time analyzer: %s\n", strerror(errno));
        analyzers_storage[i].offline_log_analyzer = offline_log_analyzer_init(analyzers_storage[i].logger);
        if (analyzers_storage[i].offline_log_analyzer == NULL)
            PERR("Couldn't create the offline log analyzer: %s\n", strerror(errno));
    }

    rt_log_stats_init();
    elk_logger_writer_init();
    log_manager = log_manager_init();
    if (log_manager == NULL)
        PERR("Couldn't create the log manager: %s\n", strerror(errno));
    log_server_init();

    // connect the logging mechanism
    for (i = 0; i < devices[current_device_index].flash_nb; i++) {
        log_manager_add_analyzer(log_manager, analyzers_storage[i].rt_log_analyzer);
    }
    log_manager_subscribe(log_manager, (MonitorHook) log_server_update, NULL);
    log_server_on_reset(reset_analyzers);

    // run the threads
    pthread_create(&log_server_thread, NULL, log_server_run, NULL);
    pthread_create(&log_manager_thread, NULL, log_manager_run, log_manager);
    for (i = 0; i < devices[current_device_index].flash_nb; i++) {
        // run rt log analyzer
        pthread_create(&(analyzers_storage[i].rt_log_analyzer_thread), NULL, rt_log_analyzer_run,
                analyzers_storage[i].rt_log_analyzer);
        // run offline log analyzer
        pthread_create(&(analyzers_storage[i].offline_log_analyzer_thread), NULL,
                offline_log_analyzer_run, analyzers_storage[i].offline_log_analyzer);
    }

    PINFO("Log server opened\n");
    PINFO("Browse to http://127.0.0.1:%d/ to see the current statistics\n", LOG_SERVER_PORT);
}

void TERM_LOG_MANAGER(void)
{
    uint32_t i;

    // alert the different threads to stop
    for (i = 0; i < devices[current_device_index].flash_nb; i++)
    {
        analyzers_storage[i].rt_log_analyzer->exit_loop_flag = 1;
        analyzers_storage[i].offline_log_analyzer->exit_loop_flag = 1;
    }
    log_manager->exit_loop_flag = 1;
    log_server.exit_loop_flag = 1;

    // wait for the different threads to stop, and clear their data
    for (i = 0; i < devices[current_device_index].flash_nb; i++) {
        pthread_join(analyzers_storage[i].rt_log_analyzer_thread, NULL);
        pthread_join(analyzers_storage[i].offline_log_analyzer_thread, NULL);
        rt_log_analyzer_free(analyzers_storage[i].rt_log_analyzer, 1);
        offline_log_analyzer_free(analyzers_storage[i].offline_log_analyzer);
    }
    rt_log_stats_free();
    pthread_join(log_manager_thread, NULL);
    log_manager_free(log_manager);
    log_server_stop();
    pthread_join(log_server_thread, NULL);
    log_server_free();

    elk_logger_writer_free();

    // free allocated memory
    free(analyzers_storage);
}

Logger_Pool* GET_LOGGER(unsigned int flash_number) {
    if (flash_number < devices[current_device_index].flash_nb)
        return analyzers_storage[flash_number].logger;
    return NULL;
}
