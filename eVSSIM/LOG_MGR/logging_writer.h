/*
 * Copyright 2020 The Open University of Israel
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

#ifndef LOGGING_WRITER_H__
#define LOGGING_WRITER_H__

#include <pthread.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

#define SCRATCHBOOK_SIZE 256

struct Log;
struct Logger_Pool;


typedef struct {
    /** LoggerWriter thread */
    pthread_t log_server_thread;
    /** File that the LoggerWriter works with */
    FILE *log_file[2];
    /** Curr log file to be used */
    int curr_log_file;
    /** File path that the LoggerWriter works with */
    char log_file_path[2][SCRATCHBOOK_SIZE];
    /** Maximum size of a single log file */
    uint32_t log_file_size;
    /** Current log file size */
    uint32_t curr_size;
    /** Just a thought, for logging backened to tell writing logger that it should work - can be done via message queue? */
    uint32_t message_queue;
    bool running;
} LoggerWriter;

typedef enum {
    LOGGING_WRITER_IS_OK = 0,
    LOGGING_WRITER_CRITICAL_ERROR,
    LOGGIGN_WRITER_RETVAL_N
} logging_writer_retval;

/**
 * @brief Creator of LoggerWriter object
 */
void logging_writer_init(void);

/**
 * @brief Destructor of LoggerWriter object
 */
void logging_writer_free(LoggerWriter *obj);

/**
 * @brief Getter for file name the LoggerWriter works with
 *
 * @param obj LoggerWriter object
 * @return char* The file path LoggerWriter is working with
 */
char *logging_writer_get_file_name(LoggerWriter *obj); // TODO: do we really need this?

/**
 * @brief Get the file path for the current log file
 *
 */
char *logging_writer_get_log_file_path(LoggerWriter *obj);

/**
 * @brief Save a log to Logger file
 *
 * @param log_obj Pointer to Log Object to be saved
 */
void logging_writer_save_log_to_file(struct Log *log_obj);


#endif /* LOGGING_WRITER_H__ */
