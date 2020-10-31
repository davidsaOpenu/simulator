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
#define NUM_LOG_FILES 2

struct Log;

typedef struct logger_writer_queue_node logger_writer_queue_node;
struct logger_writer_queue_node {
    struct Log *log_obj;
    unsigned int size;
    struct logger_writer_queue_node *next;
    struct logger_writer_queue_node *prev;
};

typedef struct {
    /** LoggerWriter thread */
    pthread_t log_server_thread;
    /** File that the LoggerWriter works with */
    FILE *log_file[NUM_LOG_FILES];
    /** Curr log file to be used */
    int curr_log_file;
    /** File path that the LoggerWriter works with */
    char log_file_path[NUM_LOG_FILES][SCRATCHBOOK_SIZE];
    /** Maximum size of a single log file */
    uint32_t log_file_size;
    /** Current log file size */
    uint32_t curr_size;
    /**
     * logging backened tells logging_writer that it has Logs that it has to save to file
     * queue->next points to HEAD of queue
     * queue->prev point to TAIL of queue
     */
    logger_writer_queue_node *queue;
    /**
     * The lock of the logger writer to update it safely from threads
     */
    pthread_mutex_t lock;
    bool running;
} LoggerWriter;

typedef enum {
    LOGGING_WRITER_IS_OK = 0,
    LOGGING_WRITER_ERROR,
    LOGGIGN_WRITER_RETVAL_N
} logging_writer_retval;

/**
 * @brief Creator of LoggerWriter object
 */
void logging_writer_init(void);

/**
 * @brief Destructor of LoggerWriter object
 */
void logging_writer_free(void);

/**
 * @brief Get current Logging Writer file path
 *
 * @return On success returns path to a file, otherwise NULL
 */
char *logging_writer_get_current_log_file_path(void);

/**
 * @brief Get Logging Writer file path at index
 *
 * @param[in] file_index Index of a file to get file path of
 * @return On success returns path to a file, otherwise NULL
 */
char *logging_writer_get_log_file_path_at_index(uint8_t file_index);

/**
 * @brief Save a log to Logger file
 *
 * @param log_obj Pointer to Log Object to be saved
 */
void logging_writer_save_log_to_file(struct Log *log_obj);

/**
 * @brief Enqueue Log to be written to file
 *
 * @param log_obj Log to be written to file
 * @param buffer_size Size of log buffer to be written
 */
void logging_writer_enqueue_log_for_write(struct Log *log_obj, uint32_t buffer_size);

#endif /* LOGGING_WRITER_H__ */
