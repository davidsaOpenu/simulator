/*
 * Copyright 2017 The Open University of Israel
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __LOGGING_OFFLINE_ANALYZER_H__
#define __LOGGING_OFFLINE_ANALYZER_H__

#include "logging_parser.h"

// The path for all the log filess
#define ELK_LOGGER_WRITER_LOGS_PATH "/code/logs/"

#define FILEBEAT_LOG_PATH "/logs/"
/**
* maximum number of acceptable unshipped logs by elk
*/
#define MAX_UNSHIPPED_LOGS 3

//the maximum size for the name of a log file
#define LOG_FILE_NAME_SIZE NAME_MAX

/*
 * offline analyzer loop timeout in microsecond intervals
 * used to not busy waiting
 */
#define OFFLINE_ANALYZER_LOOP_TIMEOUT_US 100000

/**
 * Used for opening files where the logs are stored
 */
#define OPEN_FROM_LOGS(FILE, MODE) fopen(ELK_LOGGER_WRITER_LOGS_PATH FILE,MODE)

/**
 * The offline log analyzer structure
 */
typedef struct {
    /**
     * The logger pool to analyze
     */
    Logger_Pool* logger_pool;
    /**
     * Whether the analyzer should stop looping ASAP
     */
    unsigned int exit_loop_flag : 1;
} OfflineLogAnalyzer;

/**
 * Create a new offline log analyzer
 * @param logger_pool the logger pool to analyze
 * @return the newly created real time log analyzer
 */
OfflineLogAnalyzer* offline_log_analyzer_init(Logger_Pool* logger_pool);

/**
 * run the log analyzer
 * @param analyzer
 * @return NULL
 */
void* offline_log_analyzer_run(void* analyzer);

/**
 * Do the main loop of the offline analyzer
 */
void offline_log_analyzer_loop(OfflineLogAnalyzer* analyzer);

/**
 * Free the offline analyzer
 * @param analyzer the offline analyzer to free
 */
void offline_log_analyzer_free(OfflineLogAnalyzer* analyzer);

/**
 * @brief Creator of LoggerWriter object
 */
void elk_logger_writer_init(void);

/**
 * @brief Destructor of LoggerWriter object
 */
void elk_logger_writer_free(void);

/**
 * @brief Save a log to Logger file
 *
 * @param log_obj Pointer to Log Object to be saved
 */
void elk_logger_writer_save_log_to_file(Byte *buffer, int length);

#endif
