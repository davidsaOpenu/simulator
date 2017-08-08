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

/*
 * The backend logging mechanism is implemented using one long array in memory, which contains a circular array inside
 * The array is aligned in memory, for a better caching performance
 * WARNING: the logging implemented here supports only one consumer and one producer!!
 */

#include <stdlib.h>
#include <string.h>

#include "logging_backend.h"


/*
 * Return the size of the logger
 * `logger` is a Logger*
 */
#define LOGGER_SIZE(logger) (((logger->head - logger->tail) + LOGGER_MAX_SIZE) % LOGGER_MAX_SIZE)

/*
 * Create a new logger and return it
 */
Logger* logger_init(void) {
    // first try to create the lock and the condition variables, to avoid malloc-free in case of a failure
    pthread_mutex_t lock;
    pthread_cond_t buffer_full, buffer_empty;
    if (pthread_mutex_init(&lock, NULL) ||
        pthread_cond_init(&buffer_full, NULL) ||
        pthread_cond_init(&buffer_empty, NULL))
        return NULL;

    // next, try to allocate the logger
    Logger* logger = (Logger*) malloc(sizeof(Logger));
    if (logger == NULL)
        return NULL;

    // finally, update the fields and return the newly created logger
    logger->head = logger->tail = &(logger->buffer);
    logger->lock = lock;
    logger->buffer_full = buffer_full;
    logger->buffer_empty = buffer_empty;
    return logger;
}

/*
 * Write `length` data units from the buffer to the logger
 * If the logger can not fit all the data, block until it can fit them all
 */
void logger_write(Logger* logger, logger_data_t* buffer, int length) {
    // wait until the buffer is empty enough
    pthread_mutex_lock(&(logger->lock));
    while (LOGGER_MAX_SIZE - LOGGER_SIZE(logger) - 1 < length) // make sure at least one slot is always empty, to sign full logger
        pthread_cond_wait(&(logger->buffer_full));
    pthread_mutex_unlock(&(logger->lock));

    // first, copy data until the end of the buffer of the logger
    int first_part_length = length;
    logger_data_t* new_head = logger->head + length;
    if ((logger->head - &(logger->buffer)) + length >= LOGGER_MAX_SIZE) {
        first_part_length = LOGGER_MAX_SIZE - (logger->head - &(logger->buffer));
        new_head = &(logger->buffer);
    }
    memcpy((void*) logger->head, (void*) buffer, first_part_length * sizeof(logger_data_t));
    logger->head = new_head;

    // next, copy the rest of the data (if any left)
    memcpy((void*) logger->head, (void*) (buffer + first_part_length), (length - first_part_length) * sizeof(logger_data_t));
    logger->head += length - first_part_length;

    // signal the consumer if necessary
    pthread_mutex_lock(&(logger->lock));
    if (LOGGER_SIZE(logger) > EMPTY_LOG_THREASHOLD)
        pthread_cond_signal(&(logger->buffer_empty));
    pthread_mutex_unlock(&(logger->lock));
}

/*
 * Read `length` data units from the logger to the buffer.
 * If the logger does not have `length` data units, block until it has enough data
 */
void logger_read(Logger* logger, logger_data_t* buffer, int length) {
    // wait until the buffer is full enough
    pthread_mutex_lock(&(logger->lock));
    while (LOGGER_SIZE(logger) < length)
        pthread_cond_wait(&(logger->buffer_empty));
    pthread_mutex_unlock(&(logger->lock));

    // first, copy data until the end of the buffer of the logger
    int first_part_length = length;
    logger_data_t* new_tail = logger->tail + length;
    if ((logger->tail - &(logger->buffer)) + length >= LOGGER_MAX_SIZE) {
        first_part_length = LOGGER_MAX_SIZE - (logger->tail - &(logger->buffer));
        new_tail = &(logger->buffer);
    }
    memcpy((void*) buffer, (void*) logger->tail, first_part_length * sizeof(logger_data_t));
    logger->tail = new_tail;

    // next, copy the rest of the data (if any left)
    memcpy((void*) (buffer + first_part_length), (void*) logger->tail, (length - first_part_length) * sizeof(logger_data_t));
    logger->tail += length - first_part_length;

    // signal the producer if necessary
    pthread_mutex_lock(&(logger->lock));
    if (LOGGER_MAX_SIZE - LOGGER_SIZE(logger) > FULL_LOG_THREASHOLD)
        pthread_cond_signal(&(logger->buffer_full));
    pthread_mutex_lock(&(logger->lock));
}

/*
 * Free the logger
 */
void logger_free(Logger* logger) {
    pthread_cond_destroy(&(logger->buffer_full));
    pthread_cond_destroy(&(logger->buffer_empty));
    pthread_mutex_destroy(&(logger->lock));
    free((void*) logger);
}
