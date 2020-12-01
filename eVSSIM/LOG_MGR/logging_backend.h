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

#ifndef __LOGGING_BACKEND_H__
#define __LOGGING_BACKEND_H__

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <pthread.h>

/**
 * The backend logging mechanism is implemented using pool of logs each one of size LOG_SIZE.
 * The Logger_Pool manges a doubly linked list of logs, Where each log holds the logged data.
 * The number of logs in each Looger_Pool allocated according to the request of each consumer.
 */


/**
 * The alignment to use when allocating the logger's buffer
 */
#define LOGGER_BUFFER_ALIGNMENT 64

/**
 * The log size to use when allocating log's in the logger pool
 */
#define LOG_SIZE (1 << 19)

/**
 * Defualt number of log's in each Looger Pool
 */
#define DEFUALT_LOGGER_POOL_SIZE 20

/**
 * Maximum time that a log can be unused and not reduced
 */
#define LOG_MAX_UNUSED_TIME_SECONDS 60

/**
 * A byte typedef, for easier use
 */
typedef unsigned char Byte;

/* Size of file path */
#define SCRATCHBOOK_SIZE 256
/* number of log files */
#define NUM_LOG_FILES 2

/**
 * The Log structure
 * Each Log represents a place in the Logger pool
 * Where the program may write/read data from
 */
typedef struct Log Log;
struct Log {
    /**
     * The main buffer of the logger log
     */
    Byte* buffer;
    /**
     * The next place to insert a byte at the buffer
     */
    Byte* head;
    /**
     * The next place to read a byte from the buffer
     */
    Byte* tail;
    /**
     * The next log
     */
    Log* next;
    /**
     * The previous log
     */
    Log* prev;
    /**
     * Timestamp of last write to the log
     */
    time_t last_used_timestamp;
    /**
     *  Flag that indicates if this Log is Clean
     *  The Log is clean in two cases:
     *      - newly created Log that wasn't used yet
     *      - Log that was fully read and the data in it
     *        is not needed any more
     */
    bool clean;
    /**
     * Flag that indicates if the real time analyzer
     * done reading this log
     */
    bool rt_analyzer_done;
    /**
     * Flag that indicates if the offline analyzer
     * done reading this log
     */
    bool offline_analyzer_done;
};

/**
 * The Logger Pool structure
 * Each logger pool holds number_of_allocated_logs Logs
 */
typedef struct {
    /**
     * Next free log of Logger pool
     */
    Log* free_log;
    /**
     * dummy log
     * This log holds the tail log and head log of the linked list
     * head log = dummy_log->next
     * tail log = dummy_log->prev
     */
    Log* dummy_log;
    /**
     * Number of total free logs in the Logger pool
     */
    unsigned int number_of_free_logs;
    /**
     * Number of allocated logs in the pool
     * This number doesn't change after the first allocation
     * of Logger_Pool
     */
    unsigned int number_of_allocated_logs;
    /**
     * Current number of logs in the Logger Pool
     * This number changes each time we allocate more Logs
     */
    unsigned int current_number_of_logs;

    /**
     * The lock of the logger pool to update it safely from
     * threads
     */
    pthread_mutex_t lock;
} Logger_Pool;

typedef struct {
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
     * The lock of the logger writer to update logger file safely from threads
     */
    pthread_mutex_t lock;
} logger_writer;

/**
 * Create a new logger
 * @param number_of_logs the number of logs to allocate at this logger pool
 *  Due to the way posix_memalign works, size must be a power of two multiple of sizeof(void *)
 * @return the newly created logger
 */
Logger_Pool* logger_init(unsigned int number_of_logs);

/**
 * Write a byte array to the log
 * Print a warning to stderr if the log cannot fit all the data
 * @param logger_pool struct that holds the logs that we write the bytes to
 * @param buffer the buffer to read the bytes from
 * @param length the number of bytes to copy from the buffer to the logger
 * @return 0 if the logger can fit all the data, otherwise nonzero
 */
int logger_write(Logger_Pool* logger_pool, Byte* buffer, int length);

/**
 * Read a byte array from the logger
 * @param logger_pool the logger pool to read the data from
 * @param buffer the buffer to write the data to
 * @param length the maximum number of bytes to read
 * @return the number of bytes read
 */
int logger_read(Logger_Pool* logger_pool, Byte* buffer, int length);

/**
 * Read a byte array from the log
 * @param log the log to read the data from
 * @paratm buffer the buffer to write the data to
 * @paratm length the maximum number of bytes to read
 * @return the number of bytes to read
 */
int logger_read_log(Log* log, Byte* buffer, int length);

/**
 * Free the logger_pool
 * @param logger_pool the logger pool to free
 */
void logger_free(Logger_Pool* logger_pool);

/**
 * Reduce logger pool size to it's original allocated size
 * logger_reduce_size() has to be called before the use of logger_clean()
 * to have any affect
 * @param logger_pool the looger pool to reduce it's size
 */
void logger_reduce_size(Logger_Pool* logger_pool);

/**
 * Clean logs that were written and read by
 * the real time analyzer and the offline analyzer
 * logger_clean() must be called after logger_reduce_size() for the
 * reduction of the logger size to have affect
 * @param logger_pool the logger pool that hold's the log's to clean
 */
void logger_clean(Logger_Pool* logger_pool);

/**
 * @brief Creator of LoggerWriter object
 */
void logger_writer_init(void);

/**
 * @brief Destructor of LoggerWriter object
 */
void logger_writer_free(void);

/**
 * @brief Getter for file name the LoggerWriter works with
 *
 * @param obj LoggerWriter object
 * @return char* The file path LoggerWriter is working with
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
void logger_writer_save_log_to_file(Byte *buffer, int length);

#endif
