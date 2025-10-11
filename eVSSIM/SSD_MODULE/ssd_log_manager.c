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
 * The loggers' + real time analyzers' data (per device, per flash)
 */
LoggerAnalyzerStorage** analyzers_storage = NULL;
/**
 * The log manager thread (per device)
 */
pthread_t* log_manager_thread = NULL;
/**
 * The log server thread
 */
pthread_t log_server_thread;
/**
 * Flag to track if log server is initialized
 */
static bool log_server_initialized = false;

void reset_analyzers(uint8_t device_index) {
    if (analyzers_storage == NULL || analyzers_storage[device_index] == NULL) {
        return;
    }
    
    uint32_t i;
    for (i = 0; i < devices[device_index].flash_nb; i++)
        analyzers_storage[device_index][i].rt_log_analyzer->reset_flag = 1;
}

void INIT_LOG_MANAGER(uint8_t device_index)
{
    uint32_t i;

    if (analyzers_storage[device_index] != NULL)
    {
        // Already inited for this device
        return;
    }

    // allocate memory
    analyzers_storage[device_index] = (LoggerAnalyzerStorage*)malloc(sizeof(LoggerAnalyzerStorage) * devices[device_index].flash_nb);
    if (analyzers_storage[device_index] == NULL)
        PERR("Couldn't allocate memory for the loggers and real time analyzers: %s\n",
                strerror(errno));

    // init structures
    for (i = 0; i < devices[device_index].flash_nb; i++) {
        analyzers_storage[device_index][i].logger = logger_init(DEFUALT_LOGGER_POOL_SIZE);
        if (analyzers_storage[device_index][i].logger == NULL)
            PERR("Couldn't create the logger: %s\n", strerror(errno));
        analyzers_storage[device_index][i].rt_log_analyzer = rt_log_analyzer_init(analyzers_storage[device_index][i].logger, i);
        if (analyzers_storage[device_index][i].rt_log_analyzer == NULL)
            PERR("Couldn't create the real time analyzer: %s\n", strerror(errno));
        analyzers_storage[device_index][i].offline_log_analyzer = offline_log_analyzer_init(analyzers_storage[device_index][i].logger);
        if (analyzers_storage[device_index][i].offline_log_analyzer == NULL)
            PERR("Couldn't create the offline log analyzer: %s\n", strerror(errno));
    }

    rt_log_stats_init(device_index);
    elk_logger_writer_init();
    log_manager[device_index] = log_manager_init();
    if (log_manager[device_index] == NULL)
        PERR("Couldn't create the log manager: %s\n", strerror(errno));
    
    // Initialize log server once for all devices
    if (!log_server_initialized) {
        if (log_server_init(0) == 0) {
            log_server_on_reset(reset_analyzers);
            pthread_create(&log_server_thread, NULL, log_server_run, NULL);
            log_server_initialized = true;
            PINFO("Log server opened\n");
            PINFO("Browse to http://127.0.0.1:%d/ to see the current statistics\n", LOG_SERVER_PORT(0));
        }
    }

    // connect the logging mechanism
    for (i = 0; i < devices[device_index].flash_nb; i++) {
        log_manager_add_analyzer(log_manager[device_index], analyzers_storage[device_index][i].rt_log_analyzer);
    }
    log_manager_subscribe(log_manager[device_index], (MonitorHook) log_server_update, NULL);

    log_manager_run_args_t* run_args = (log_manager_run_args_t*)malloc(sizeof(log_manager_run_args_t));
    if (run_args == NULL) {
        PERR("Malloc for log manager run args failed!\n");
    }
    run_args->device_index = device_index;
    run_args->manager = log_manager[device_index];

    pthread_create(&log_manager_thread[device_index], NULL, log_manager_run, run_args);
    for (i = 0; i < devices[device_index].flash_nb; i++) {
        // Run rt log analyzer
        rt_log_analyzer_run_args_t* log_analyzer_run_args = (rt_log_analyzer_run_args_t*)malloc(sizeof(rt_log_analyzer_run_args_t));
        if (NULL == log_analyzer_run_args)
        {
            PERR("Malloc for rt log analyzer failed!\n");
        }
        log_analyzer_run_args->device_index = device_index;
        log_analyzer_run_args->analyzer = analyzers_storage[device_index][i].rt_log_analyzer;
        pthread_create(&(analyzers_storage[device_index][i].rt_log_analyzer_thread), NULL, rt_log_analyzer_run,
                log_analyzer_run_args);

        // Run offline log analyzer
        pthread_create(&(analyzers_storage[device_index][i].offline_log_analyzer_thread), NULL,
                offline_log_analyzer_run, analyzers_storage[device_index][i].offline_log_analyzer);
    }

    PINFO("Log manager initialized for device %d\n", device_index);
}

void TERM_LOG_MANAGER(uint8_t device_index)
{
    uint32_t i;

    if (analyzers_storage == NULL || analyzers_storage[device_index] == NULL)
    {
        // Already termed or never inited for this device
        return;
    }

    // alert the different threads to stop
    for (i = 0; i < devices[device_index].flash_nb; i++)
    {
        analyzers_storage[device_index][i].rt_log_analyzer->exit_loop_flag = 1;
        analyzers_storage[device_index][i].offline_log_analyzer->exit_loop_flag = 1;
    }
    log_manager[device_index]->exit_loop_flag = 1;

    // wait for the different threads to stop, and clear their data
    for (i = 0; i < devices[device_index].flash_nb; i++) {
        pthread_join(analyzers_storage[device_index][i].rt_log_analyzer_thread, NULL);
        pthread_join(analyzers_storage[device_index][i].offline_log_analyzer_thread, NULL);
        rt_log_analyzer_free(analyzers_storage[device_index][i].rt_log_analyzer, 1);
        offline_log_analyzer_free(analyzers_storage[device_index][i].offline_log_analyzer);
    }
    rt_log_stats_free(device_index);
    pthread_join(log_manager_thread[device_index], NULL);
    log_manager_free(log_manager[device_index]);
    log_manager[device_index] = NULL;

    elk_logger_writer_free();

    // free allocated memory for this device
    free(analyzers_storage[device_index]);
    analyzers_storage[device_index] = NULL;
    
    // Check if all devices are terminated, then stop log server
    bool all_terminated = true;
    for (i = 0; i < device_count; i++) {
        if (analyzers_storage[i] != NULL) {
            all_terminated = false;
            break;
        }
    }
    
    if (all_terminated && log_server_initialized) {
        log_server.exit_loop_flag = 1;
        log_server_stop();
        pthread_join(log_server_thread, NULL);
        log_server_free();
        log_server_initialized = false;
        PINFO("Log server stopped\n");
    }
}

Logger_Pool* GET_LOGGER(uint8_t device_index, unsigned int flash_number) {
    if (analyzers_storage == NULL || analyzers_storage[device_index] == NULL) {
        return NULL;
    }
    if (flash_number < devices[device_index].flash_nb)
        return analyzers_storage[device_index][flash_number].logger;
    return NULL;
}
