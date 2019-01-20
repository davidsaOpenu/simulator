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
 * The time diff to use when trying to reduce
 * logger_pool size
 */
#define CLEAN_TIME_DIFF 60

/**
 * Defualt number of log's in each Looger Pool
 */
#define DEFUALT_LOGGER_POOL_SIZE 20

/**
 * A byte typedef, for easier use
 */
typedef unsigned char Byte;


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
     * Map of block number to number of current
     * written pages
     */
    void* block_map_buffer;

    /**
     * The lock of the logger pool to update it safely from
     * threads
     */
    pthread_mutex_t lock;
} Logger_Pool;

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
 * Free the logger_pool
 * @param logger_pool the logger pool to free
 */
void logger_free(Logger_Pool* logger_pool);

/**
 * TODO
 */
void logger_pool_reduce_size(Logger_Pool* logger_pool);

#endif
