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
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <linux/unistd.h>
#include <json.h>

#include "logging_backend.h"

/**
 * Return the number of empty bytes in the log
 * @param {Log*} log to check
 * @return the number of empty bytes in the log
 */
#define NUMBER_OF_BYTES_TO_WRITE(log) (LOG_SIZE - (log->head - log->buffer))

/**
 * Return the number of full bytes that left to read in the log
 * @param {Log*} log to check
 * @return the number of full bytes that left to read in the log
 */
#define NUMBER_OF_BYTES_TO_READ(log) (LOG_SIZE - (log->tail - log->buffer))

/**
 * Return true if current number of allocated logs in the logger pool
 * bigger then the number of allocated logs
 * @param {Logger_Pool*} logger pool to test if can reduce number of allocated logs
 * @return true if can reduce number of logs false otherwise
 */
#define REDUCE_LOGGER_POOL(logger_pool) \
    ((logger_pool->current_number_of_logs > logger_pool->number_of_allocated_logs) ? true : false)

/**
 * Insert node as the tail node of the linked list
 */
#define INSERT_NODE(node,dummy,buf) { \
    node->next = dummy; \
    node->prev = dummy->prev; \
    node->next->prev = node; \
    node->prev->next = node; \
    node->last_used_timestamp = time(NULL); \
    node->clean = true; \
    node->rt_analyzer_done = false; \
    node->offline_analyzer_done = false; \
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


static logger_writer logger_writer_obj;


Logger_Pool* logger_init(unsigned int number_of_logs) {
    Byte *buffer;
    Log *log, *dummy;
    uint32_t i;

    // There has to be at least one log in the pool
    if(number_of_logs == 0)
        return NULL;

    // allocate memory for the dummy log
    // the dummy log will act as a linked list sentinel
    dummy = (Log*) malloc(sizeof(Log));
    if (dummy == NULL) {
        return NULL;
    }

    // initialize the dummy log
    dummy->next = dummy;
    dummy->prev = dummy;

    //try to allocate number_of_logs
    for (i=0; i<number_of_logs; i++) {
        log = (Log*) malloc(sizeof(Log));
        if (log == NULL) {
            return NULL;
        }

        if (posix_memalign((void**) &buffer, LOGGER_BUFFER_ALIGNMENT, LOG_SIZE)) {
            return NULL;
        }

        INSERT_NODE(log, dummy, buffer);
    }

    // allocate Logger_Pool structre to hold and mange the pool of logs
    // that were allocated above
    Logger_Pool* logger_pool = (Logger_Pool*) malloc(sizeof(Logger_Pool));
    if(logger_pool == NULL)
        return NULL;

    // try to create the lock
    if (pthread_mutex_init(&logger_pool->lock, NULL))
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

    // lock logger pool
    pthread_mutex_lock(&logger_pool->lock);

    // make sure that logger pool allways has at least 2 logs
    // one log that we write to and another one for when
    // the first one get full
    if(logger_pool->number_of_free_logs == 1)
    {
        Log* log = (Log*) malloc(sizeof(Log));
        if(log == NULL)
        {
            // unlock logger pool
            pthread_mutex_unlock(&logger_pool->lock);
            return 1;
        }

        if (posix_memalign((void**) &mem_buf, LOGGER_BUFFER_ALIGNMENT, LOG_SIZE))
        {
            // unlock logger pool
            pthread_mutex_unlock(&logger_pool->lock);
            return 1;
        }

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

    // unlock logger pool
    pthread_mutex_unlock(&logger_pool->lock);

    return res;
}

int logger_read(Logger_Pool* logger_pool, Byte* buffer, int length) {
    Log *log, *temp_log;
    int number_bytes_to_read_in_log; // number of bytes that wasn't read yet in the log
    int number_of_bytes_to_read = length; // number of bytes that we still have to read

    // lock logger pool
    pthread_mutex_lock(&logger_pool->lock);

    log = logger_pool->dummy_log->next;

    while(number_of_bytes_to_read > 0)
    {
        // if the log is not clean and the real time analyzer have not
        // finished to read from it then read length bytes of data
        if(!(log->clean) && !(log->rt_analyzer_done))
        {
            // check how many bytes left to read in this log
            number_bytes_to_read_in_log = NUMBER_OF_BYTES_TO_READ(log);

            if(number_bytes_to_read_in_log >= number_of_bytes_to_read) // read length bytes from this log
            {
                // check that we are not going to read past what has been written
                if(!((NUMBER_OF_BYTES_TO_READ(log)-number_of_bytes_to_read) >=
                            NUMBER_OF_BYTES_TO_WRITE(log)))
                {
                    // unlock logger pool
                    pthread_mutex_unlock(&logger_pool->lock);
                    return (length-number_of_bytes_to_read);
                }

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
                {
                    // unlock logger pool
                    pthread_mutex_unlock(&logger_pool->lock);
                    return (length-number_of_bytes_to_read);
                }

                memcpy((void*) buffer, (void*) log->tail, number_bytes_to_read_in_log);
                buffer += number_bytes_to_read_in_log;
                log->tail += number_bytes_to_read_in_log;
                number_of_bytes_to_read -= number_bytes_to_read_in_log;
            }
        }

        // save log->next
        temp_log = log->next;

        // check if we are done reading this log
        if(NUMBER_OF_BYTES_TO_READ(log) == 0)
            log->rt_analyzer_done = true;

        log = temp_log;

        // check if we reached to the end of the Logger_Pool linked list
        if(log == logger_pool->dummy_log)
        {
            // unlock logger pool
            pthread_mutex_unlock(&logger_pool->lock);
            return (length-number_of_bytes_to_read);
        }
    }

    // unlock logger pool
    pthread_mutex_unlock(&logger_pool->lock);
    return (length-number_of_bytes_to_read);
}

int logger_read_log(Log* log, Byte* buffer, int length) {
    // check the length is less the maximum log length
    if( length > LOG_SIZE )
        return 0;

    memcpy((void*) buffer, (void*) log->buffer, length);

    return length;
}

void logger_free(Logger_Pool* logger_pool) {
    Log* log = logger_pool->dummy_log->next;
    Log* prev;

    // lock logger pool
    pthread_mutex_lock(&logger_pool->lock);

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

    // unlock logger pool
    pthread_mutex_unlock(&logger_pool->lock);

    free(logger_pool);
}

void logger_reduce_size(Logger_Pool* logger_pool) {
    Log* log = logger_pool->dummy_log->next;
    Log* temp_log;

    // lock logger pool
    pthread_mutex_lock(&logger_pool->lock);

    // if the current number of logs in the pool is greater then the nummber of
    // logs that were allocated when the logger pool was initialized
    // loop through the logger pool and try to reduce it's size back to
    // number of logs = number_of_allocated_logs
    while( REDUCE_LOGGER_POOL(logger_pool) && ( log != logger_pool->dummy_log ))
    {
        temp_log = log->next;
        // make sure that the analayzers are done with this log
        // and that it wasn't used for at least LOG_MAX_UNUSED_TIME_SECONDS
        if(!(log->clean) && log->rt_analyzer_done && log->offline_analyzer_done &&
                ((time(NULL) - log->last_used_timestamp) > LOG_MAX_UNUSED_TIME_SECONDS))
        {
            DISCONNECT_NODE(log);
            free(log->buffer);
            free(log);

            // update logger pool atributes
            logger_pool->current_number_of_logs -= 1;
        }

        log = temp_log;
    }

    // unlock logger pool
    pthread_mutex_unlock(&logger_pool->lock);
}

void logger_clean(Logger_Pool* logger_pool) {
    Log* log = logger_pool->dummy_log->next;
    Log* temp_log;

    // lock logger pool
    pthread_mutex_lock(&logger_pool->lock);

    while(log != logger_pool->dummy_log)
    {
        temp_log = log->next;
        // check if both rt_analayzer and offline_analyzer
        // done reading from the log
        // if true clean the log for next reuse
        if(!(log->clean) && log->rt_analyzer_done && log->offline_analyzer_done)
        {
            DISCONNECT_NODE(log);
            INSERT_NODE(log, logger_pool->dummy_log, log->buffer);
            logger_pool->number_of_free_logs++;
        }
        log = temp_log;
    }

    // unlock logger pool
    pthread_mutex_unlock(&logger_pool->lock);
}


static int logger_writer_open_file_for_write(void) {
    uint8_t file_num = 0;

    while (file_num < NUM_LOG_FILES) {
        logger_writer_obj.log_file[file_num] = fopen(logger_writer_obj.log_file_path[file_num], "w");

        if (NULL == logger_writer_obj.log_file[file_num]) {
            uint8_t file_to_close = 0;
            while (file_to_close <= file_num) {
                if (fclose(logger_writer_obj.log_file[file_to_close]) != 0) {
                }
                file_to_close++;
            }
            return -1;
        } else {
            file_num++;
        }
    }

    return 0;
}

static void logger_writer_close_file(void) {
    uint8_t file_to_close = 0;

    while (file_to_close < NUM_LOG_FILES) {
        fclose(logger_writer_obj.log_file[file_to_close]);
        file_to_close++;
    }
}

void logger_writer_init(void) {
    // MB: see following comment
    //  srand(time(0));
    //  int random_index;
    uint8_t file_index = 0;

    while (file_index < NUM_LOG_FILES) {
        // MB: Random index may cause collisions in file path, thus commented:
        //  random_index = rand() % 100;
        sprintf(logger_writer_obj.log_file_path[file_index], "/tmp/log_file_%d.log", file_index);
        file_index++;
    }

    logger_writer_obj.log_file_size = 1024 * 1024;

    logger_writer_obj.curr_log_file = 0;
    logger_writer_obj.curr_size = 0;
    int retval = logger_writer_open_file_for_write();

    if (retval == -1) {
        logger_writer_free();
        return;
    }
}

void logger_writer_free(void) {
    logger_writer_close_file();
}

char *logger_writer_get_current_log_file_path(void) {
    return logger_writer_obj.log_file_path[logger_writer_obj.curr_log_file];
}

char *logger_writer_get_log_file_path_at_index(uint8_t file_index) {
    char *log_file_path = NULL;

    if (file_index < NUM_LOG_FILES) {
        log_file_path = logger_writer_obj.log_file_path[file_index];
    }
    return log_file_path;
}

void logger_writer_save_log_to_file(Byte *buffer, int length) {
    int file_to_write;

    if (NULL != buffer) {
        // I lock here in order to prevent collisions between the different rt_analyzer threads while the touch
        //  writer_obj related members.
        pthread_mutex_lock(&logger_writer_obj.lock);
        // Check if there is enought space in the current log file to write log
        if (length + logger_writer_obj.curr_size > logger_writer_obj.log_file_size) {
            logger_writer_obj.curr_size = 0;
            logger_writer_obj.curr_log_file = (logger_writer_obj.curr_log_file + 1) % NUM_LOG_FILES;
        }
        file_to_write = logger_writer_obj.curr_log_file;

        // Increase the size of current log_file by the buffer's size
        logger_writer_obj.curr_size += length;
        pthread_mutex_unlock(&logger_writer_obj.lock);

        fwrite(buffer, sizeof(Byte), length, logger_writer_obj.log_file[file_to_write]);
    }
}
