// Copyright(c)2013
//
// Hanyang University, Seoul, Korea
// Embedded Software Systems Lab. All right reserved

#ifndef _LOG_MANAGER_H_
#define _LOG_MANAGER_H_

#include "logging_backend.h"
#include "logging_manager.h"
#include "logging_parser.h"
#include "logging_rt_analyzer.h"
#include "logging_offline_analyzer.h"
#include "logging_server.h"
#include "logging_statistics.h"
#include <pthread.h>


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

extern LoggerAnalyzerStorage** analyzers_storage;
extern pthread_t* log_manager_threads;

void INIT_LOG_MANAGER(uint8_t device_index);
void TERM_LOG_MANAGER(uint8_t device_index);

/**
 * Waits until filebeat has fully shipped every run log to ELK (registry
 * offset >= file size for each log file), then deletes the log files so
 * the logs dir is empty once the run is over.
 * @param timeout_ms maximum time to wait in milliseconds
 * @return  0 if every log file was shipped and deleted;
 *         -1 if the wait timed out while log files were still pending;
 *         -2 if the filebeat registry is not available (e.g. no ELK)
 */
int LOG_MANAGER_WAIT_UNTIL_ALL_SHIPPED(uint64_t timeout_ms);

void THREAD_SERVER(void);
void THREAD_CLIENT(void *arg);

/**
 * Returns the appropriate logger of the flash
 * @param flash_number the number of the flash
 * @return the appropriate logger of the flash, or NULL if an error occurred
 *         (or if the logger is not set up)
 */
Logger_Pool* GET_LOGGER(uint8_t device_index, unsigned int flash_number);

/**
 * Reset all the data saved in the analyzers, to clear the entire logging
 * mechanism.
 */
void reset_analyzers(uint8_t device_index);

#endif
