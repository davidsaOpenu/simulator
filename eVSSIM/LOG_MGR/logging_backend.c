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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <linux/unistd.h>

#include "logging_backend.h"

/**
 * Return the number of empty bytes in the log
 * @param {Log*} log to chcek
 * @return the number of empty bytes in the log
 */
#define NUMBER_OF_BYTES_TO_WRITE(log) (LOG_SIZE - (log->head - log->buffer))
/**
 * Return the number of full bytes that left to read in the log
 * @param {Log*} log to chcek
 * @return the number of full bytes that left to read in the log
 */
#define NUMBER_OF_BYTES_TO_READ(log) (LOG_SIZE - (log->tail - log->buffer))

/**
 * Insert node to as the tail node of the linked list
 */
#define INSERT_NODE(node,dummy,buf) { \
    node->next = dummy; \
    node->prev = dummy->prev; \
    node->next->prev = node; \
    node->prev->next = node; \
    node->last_used_timestamp = time(NULL); \
    node->clean = true; \
    node->head = node->tail = node->buffer = buf; \
}

/**
 * Disconnect node from the linked list
 */
#define DISCONNECT_NODE(node) { \
    node->prev->next = node->next; \
    node->next->prev = node->prev; \
}

/**
 * Update node metadata
 */
#define UPDATE_NODE(node, is_clean, size) { \
    node->head += size; \
    node->last_used_timestamp = time(NULL); \
    node->clean = is_clean; \
}

Logger_Pool* logger_init(unsigned int number_of_logs) {
    Byte *buffer;
    Log *log, *dummy;
    int i;

    // There has to be at least one log in the pool
    if(number_of_logs == 0)
        return NULL;

    // allocate memory for the dummy log
    // the dummy log will act as a linked list sentinel
    dummy = (Log*) malloc(sizeof(Log));
    if(dummy == NULL)
        return NULL;

    // initialize the dummy log
    dummy->next = dummy;
    dummy->prev = dummy;

    //try to allocate number_of_logs
    for(i=0; i<number_of_logs; i++) {
        log = (Log*) malloc(sizeof(Log));
        if(log == NULL)
            return NULL;

        if (posix_memalign((void**) &buffer, LOGGER_BUFFER_ALIGNMENT, LOG_SIZE))
            return NULL;

        INSERT_NODE(log, dummy, buffer);
    }

    // allocate Logger_Pool structre to hold and mange the pool of logs
    // that were allocated above
    Logger_Pool* logger_pool = (Logger_Pool*) malloc(sizeof(Logger_Pool));
    if(logger_pool == NULL)
        return NULL;

    // update logger pool atributes
    logger_pool->free_log = dummy->next;
    logger_pool->dummy_log = dummy;
    logger_pool->current_number_of_logs =
        logger_pool->number_of_allocated_logs =
        logger_pool->number_of_free_logs = number_of_logs;

    return logger_pool;
}

int logger_write(Logger_Pool* logger_pool, Byte* buffer, int length) {
    Byte* mem_buf;
    int res=0, number_of_bytes_to_write=0;

    // we support up to LOG_SIZE bytes in one write
    if(length > LOG_SIZE)
        return 1;

    // make sure that logger pool allways has at least 2 logs
    // one log that we write to and another one for when
    // the first one get full
    if(logger_pool->number_of_free_logs == 1)
    {
        Log* log = (Log*) malloc(sizeof(Log));
        if(log == NULL)
            return 1;

        if (posix_memalign((void**) &mem_buf, LOGGER_BUFFER_ALIGNMENT, LOG_SIZE))
            return 1;

        INSERT_NODE(log, logger_pool->dummy_log, mem_buf);

        // update logger pool atributes
        logger_pool->number_of_free_logs += 1;
        logger_pool->current_number_of_logs += 1;
    }

    if(NUMBER_OF_BYTES_TO_WRITE(logger_pool->free_log) >= length) // write all data to free_log
    {
        memcpy((void*) logger_pool->free_log->head, (void*) buffer, length);

        UPDATE_NODE(logger_pool->free_log, false, length);

        //chek if this log is full
        if(NUMBER_OF_BYTES_TO_WRITE(logger_pool->free_log) == 0)
        {
            // update logger pool atributes
            logger_pool->free_log = logger_pool->free_log->next;
            logger_pool->number_of_free_logs--;
        }
    }
    else /* Tested by CrossBoundryStringWriteRead test from log_mgr_tests.cc */
    {
        // write as much bytes as we can to free_log
        number_of_bytes_to_write = NUMBER_OF_BYTES_TO_WRITE(logger_pool->free_log);

        memcpy((void*) logger_pool->free_log->head, (void*) buffer, number_of_bytes_to_write);

        UPDATE_NODE(logger_pool->free_log, false, number_of_bytes_to_write);

        logger_pool->number_of_free_logs--;

        // write the rest of the bytes to the next free_log
        logger_pool->free_log = logger_pool->free_log->next;
        memcpy((void*) logger_pool->free_log->head, (void*)(buffer+number_of_bytes_to_write),
                (length-number_of_bytes_to_write));

        UPDATE_NODE(logger_pool->free_log, false, (length-number_of_bytes_to_write));
    }
    return res;
}

int logger_read(Logger_Pool* logger_pool, Byte* buffer, int length) {
    Log *log, *temp_log;
    int number_bytes_to_read_in_log; // number of bytes that wasn't read yet in the log
    int number_of_bytes_to_read = length; // number of bytes that we still have to read

    log = logger_pool->dummy_log->next;

    while(number_of_bytes_to_read > 0)
    {
        // if log is not clean lets read from it
        // length bytes of data
        if(!(log->clean))
        {
            // check how many bytes left to read in this log
            number_bytes_to_read_in_log = NUMBER_OF_BYTES_TO_READ(log);

            if(number_bytes_to_read_in_log >= number_of_bytes_to_read) // read length bytes from this log
            {
                // check that we are not going to read past what has been written
                if(!((NUMBER_OF_BYTES_TO_READ(log)-number_of_bytes_to_read) >=
                            NUMBER_OF_BYTES_TO_WRITE(log)))
                    return (length-number_of_bytes_to_read);

                memcpy((void*) buffer, (void*) log->tail, number_of_bytes_to_read);
                log->tail += number_of_bytes_to_read;
                // we read all the needed bytes
                number_of_bytes_to_read = 0;
            }
            else // read number_bytes_to_read_in_log from the current log and all the rest of the bytes from the next log
            { /* Tested by CrossBoundryStringWriteRead test from log_mgr_tests.cc */
                // check that we are not going to read past what has bean written
                if(!((NUMBER_OF_BYTES_TO_READ(log)-number_bytes_to_read_in_log) >=
                            NUMBER_OF_BYTES_TO_WRITE(log)))
                    return (length-number_of_bytes_to_read);

                memcpy((void*) buffer, (void*) log->tail, number_bytes_to_read_in_log);
                buffer += number_bytes_to_read_in_log;
                log->tail += number_bytes_to_read_in_log;
                number_of_bytes_to_read -= number_bytes_to_read_in_log;
            }
        } else {
            // The current log is clean, we have nothing to read
            return 0;
        }

        // save log->next
        temp_log = log->next;

        // check if we done reading all this log
        // if true set it as clean
        // and move it to the end of the Logger_Pool
        // in order to use it as free log
        if(NUMBER_OF_BYTES_TO_READ(log) == 0)
        {
            DISCONNECT_NODE(log);
            INSERT_NODE(log, logger_pool->dummy_log, log->buffer);
            logger_pool->number_of_free_logs++;
        }
        log = temp_log;

        // check if we reached to the end of the Logger_Pool linked list
        if(log == logger_pool->dummy_log)
            return (length-number_of_bytes_to_read);
    }
    return (length-number_of_bytes_to_read);
}

void logger_free(Logger_Pool* logger_pool) {
    Log* log = logger_pool->dummy_log->next;
    Log* prev;

    while(log != logger_pool->dummy_log)
    {
        prev = log;
        log = log->next;
        // free the logger_pool buffer
        free(prev->buffer);
        // free the log
        free(prev);
    }
    // free Logger_Pool
    free(logger_pool->dummy_log);
    free(logger_pool);
}
