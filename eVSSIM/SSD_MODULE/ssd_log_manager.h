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


void INIT_LOG_MANAGER(void);
void TERM_LOG_MANAGER(void);
void WRITE_LOG(const char *fmt, ...);

void THREAD_SERVER(void);
void THREAD_CLIENT(void *arg);

/**
 * Returns the appropriate logger of the flash
 * @param flash_number the number of the flash
 * @return the appropriate logger of the flash, or NULL if an error occurred
 *         (or if the logger is not set up)
 */
Logger_Pool* GET_LOGGER(unsigned int flash_number);

/**
 * Reset all the data saved in the analyzers, to clear the entire logging
 * mechanism.
 */
void reset_analyzers(void);


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
#endif
