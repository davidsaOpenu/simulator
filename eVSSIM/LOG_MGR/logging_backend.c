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

#include "logging_backend.h"

/**
 * Return the number of empty bytes in the log
 * @param {Log*} log to chcek
 * @return the number of empty bytes in the log
 */
#define LOGGER_NODE_EMPTY_BYTES(log) (LOGGER_BUFFER_POOL_SIZE - (log->head - log->buffer))
/**
 * Return the number of bytes in the log that left to read
 * @param {Log*} log to chcek
 * @return the number of bytes in the log that left to read
 */
#define LOGGER_NODE_FULL_BYTES(log) (LOGGER_BUFFER_POOL_SIZE - (log->tail - log->buffer))

Logger_Pool* logger_init(unsigned int number_of_logs) {
    Byte *buffer;
    Log *log,*head,*tail,*cur,*prev;
    unsigned int i;

    // There has to be at least one log in the pool
    if(number_of_logs == 0)
        return NULL;

    // initialzie Log variables
    head = tail = cur = prev = log = NULL;

    // try to allocate number_of_logs
    for(i=0; i<number_of_logs; i++) {
        log = (Log*) malloc(sizeof(Log));
        if(log == NULL)
            return NULL;

        if (posix_memalign((void**) &buffer, LOGGER_BUFFER_ALIGNMENT, LOGGER_BUFFER_POOL_SIZE))
            return NULL;

        // if this is the first log allocation set correctly the log structre
        // and save it as the head log
        if(i==0)
        {
            head = tail = cur = prev = log;
            cur->prev = NULL;
            cur->next = NULL;
            cur->head = cur->tail = cur->buffer = buffer;
            cur->last_used_timestamp = time(NULL);
            cur->clean = true;
            continue;
        }
        else
        {
            prev->next = cur = log;
            cur->prev = prev;
            cur->next = NULL;
            cur->head = cur->tail = cur->buffer = buffer;
            cur->last_used_timestamp = time(NULL);
            cur->clean = true;
            prev = cur;
        }
        // if this is the last log allocation save it as the tail log
        if(i == (number_of_logs-1))
            tail = cur;
    }

    // allocate Logger_Pool structre to hold and mange the pool of logs
    // that were allocated above
    Logger_Pool* logger_pool = (Logger_Pool*) malloc(sizeof(Logger_Pool));
    if(logger_pool == NULL)
        return NULL;

    logger_pool->head_log = head;
    logger_pool->tail_log = tail;
    logger_pool->free_log = head;
    logger_pool->current_number_of_logs = logger_pool->number_of_allocated_logs = logger_pool->number_of_free_logs = number_of_logs;

    return logger_pool;
}


int logger_write(Logger_Pool* logger_pool, Byte* buffer, int length) {
    Byte* mem_buf;
    int res=0, number_of_bytes_to_write=0;

    // check if there is a free log to write to
    // OR if there is not enough space in the log
    // for the amount of data that is going to be written and that this is the
    // last log
    if(logger_pool->number_of_free_logs == 0 ||
            (((LOGGER_NODE_EMPTY_BYTES(logger_pool->free_log)) < length) && (logger_pool->number_of_free_logs == 1)))
    {
        Log* log = (Log*) malloc(sizeof(Log));
        if(log == NULL)
            return 1;

        if (posix_memalign((void**) &mem_buf, LOGGER_BUFFER_ALIGNMENT, LOGGER_BUFFER_POOL_SIZE))
            return 1;

        log->head = log->tail = log->buffer = mem_buf;
        log->next = NULL;
        log->prev = logger_pool->tail_log;
        log->clean = true;

        logger_pool->tail_log->next = log;
        logger_pool->tail_log = log;
        logger_pool->number_of_free_logs++;
        logger_pool->current_number_of_logs++;

        // if we allocated new log because there is no free logs
        // we need to point free_log to log
        if(logger_pool->number_of_free_logs == 1)
            logger_pool->free_log = log;
    }

    if(LOGGER_NODE_EMPTY_BYTES(logger_pool->free_log) >= length) // write all data to free_log
    {
        memcpy((void*) logger_pool->free_log->head, (void*) buffer, length);
        logger_pool->free_log->head += length;
        logger_pool->free_log->last_used_timestamp = time(NULL);
        logger_pool->free_log->clean = false;

        //chek if this log is full
        if(LOGGER_NODE_EMPTY_BYTES(logger_pool->free_log) == 0)
        {
            logger_pool->free_log = logger_pool->free_log->next;
            logger_pool->number_of_free_logs--;
        }
    }
    else
    {
        // write as much bytes as we can to free_log
        number_of_bytes_to_write = LOGGER_NODE_EMPTY_BYTES(logger_pool->free_log);

        memcpy((void*) logger_pool->free_log->head, (void*) buffer, number_of_bytes_to_write);
        logger_pool->free_log->head += number_of_bytes_to_write;
        logger_pool->free_log->last_used_timestamp = time(NULL);
        logger_pool->free_log->clean = false;

        // write the rest of the bytes to the next free_log
        logger_pool->free_log = logger_pool->tail_log;
        memcpy((void*) logger_pool->free_log->head, (void*)(buffer+number_of_bytes_to_write),
                (length-number_of_bytes_to_write));
        logger_pool->free_log->head += (length-number_of_bytes_to_write);
        logger_pool->free_log->last_used_timestamp = time(NULL);
        logger_pool->free_log->clean = false;
    }
    return res;
}

int logger_read(Logger_Pool* logger_pool, Byte* buffer, int length) {
    Log* log;
    int number_bytes_to_read_in_log; // number of bytes that wasn't read yet in the log
    int number_of_bytes_to_read = length; // number of bytes that we still have to read

    log = logger_pool->head_log;
    while(number_of_bytes_to_read > 0)
    {
        // if log is not clean lets read from it
        // length bytes of data
        if(!(log->clean))
        {
            // check how many bytes left to read in this log
            number_bytes_to_read_in_log = LOGGER_NODE_FULL_BYTES(log);

            if(number_bytes_to_read_in_log >= number_of_bytes_to_read) // read length bytes from this log
            {
                memcpy((void*) buffer, (void*) log->tail, number_of_bytes_to_read);
                log->tail += number_of_bytes_to_read;
                number_of_bytes_to_read -= number_of_bytes_to_read;
            }
            else // read number_bytes_to_read_in_log from the current log and all the rest of the bytes from the next log
            {
                memcpy((void*) buffer, (void*) log->tail, number_bytes_to_read_in_log);
                log->tail += number_bytes_to_read_in_log;
                number_of_bytes_to_read -= number_bytes_to_read_in_log;
            }
        }
        // check if we done reading all this log
        // if true set it as clean
        if(log->tail == (log->buffer + LOGGER_BUFFER_POOL_SIZE))
        {
            log->clean = true;
            log->tail = log->head = log->buffer;
        }
        log = log->next;

        // check if we reached to the end of the Logger_Pool linked list
        if(log == NULL)
            return (length-number_of_bytes_to_read);
    }

    return (length-number_of_bytes_to_read);
}

void logger_free(Logger_Pool* logger_pool) {
    Log* log = logger_pool->head_log;
    Log* prev;
    while(log != NULL)
    {
        prev = log;
        log = log->next;

        // free the logger_pool buffer
        free(prev->buffer);
        // free the log
        free(prev);
    }

    // free Logger_Pool
    free(logger_pool);
}

void logger_pool_reduce_size(Logger_Pool* logger_pool) {
    Log* log = logger_pool->head_log;
    while(logger_pool->current_number_of_logs > logger_pool->number_of_allocated_logs)
    {
        if(log->clean && ((time(NULL) - log->last_used_timestamp) > CLEAN_TIME_DIFF)) // we can free this log
        {
            free(log->buffer);

            // if this log is the head_log of the loger pool
            // change the head_log of the logger pool
            if(log->prev == NULL)
                logger_pool->head_log = log->next;
            else
                log->prev->next = log->next;

            // if this log is the tail_log of the logger pool
            // change the tail_log of the logger pool
            if(log->next == NULL)
                logger_pool->tail_log = log->prev;
            else
                log->next->prev = log->prev;

            free(log);
            logger_pool->current_number_of_logs--;
        }
        log = log->next;
        if(log == NULL)
            break;
    }
}
