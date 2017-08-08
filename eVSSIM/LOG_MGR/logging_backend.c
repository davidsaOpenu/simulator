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
 * Return the size of the logger (full slots only)
 * @param {Logger*} logger the logger to check
 * @return the size of the logger (in bytes)
 * @see Logger
 */
#define LOGGER_SIZE(logger) (((logger->head - logger->tail) + logger->buffer_size) % \
                             logger->buffer_size)


Logger* logger_init(size_t size) {
    // allocate the buffer such that it is aligned to the cache line
    Byte* buffer;
    if (posix_memalign((void**) &buffer, LOGGER_BUFFER_ALIGNMENT, size))
        return NULL;

    // try to allocate the logger struct
    Logger* logger = (Logger*) malloc(sizeof(Logger));
    if (logger == NULL) {
        free((void*) buffer);
        return NULL;
    }

    // update the fields and return the newly created logger
    logger->head = logger->tail = logger->buffer = buffer;
    logger->buffer_size = size;
    return logger;
}

int logger_write(Logger* logger, Byte* buffer, int length) {
    int res = 0;
    // print a warning if the buffer is not empty enough (one empty byte indicates a full buffer)
    if (logger->buffer_size - LOGGER_SIZE(logger) - 1 < length) {
        fprintf(stderr, "WARNING: the logger's buffer may have an overflow!\n");
        fprintf(stderr, "WARNING: the log may now be corrupted and unusable!\n");
        res = 1;
    }

    // first, copy data until the end of the buffer of the logger
    int first_part_length = length;
    Byte* new_head = logger->head + length;
    if ((logger->head - logger->buffer) + length >= logger->buffer_size) {
        first_part_length = logger->buffer_size - (logger->head - logger->buffer);
        new_head = logger->buffer;
    }
    memcpy((void*) logger->head, (void*) buffer, first_part_length);
    logger->head = new_head;

    // next, copy the rest of the data (if any left)
    memcpy((void*) logger->head, (void*) (buffer + first_part_length), length - first_part_length);
    logger->head += length - first_part_length;

    return res;
}

int logger_read(Logger* logger, Byte* buffer, int length) {
    // crop the maximum length
    int logger_size = LOGGER_SIZE(logger);
    int length_read = logger_size < length ? logger_size : length;

    // first, copy data until the end of the buffer of the logger
    int first_part_length = length_read;
    Byte* new_tail = logger->tail + length_read;
    if ((logger->tail - logger->buffer) + length_read >= logger->buffer_size) {
        first_part_length = logger->buffer_size - (logger->tail - logger->buffer);
        new_tail = logger->buffer;
    }
    memcpy((void*) buffer, (void*) logger->tail, first_part_length);
    logger->tail = new_tail;

    // next, copy the rest of the data (if any left)
    memcpy((void*) (buffer + first_part_length), (void*) logger->tail,
           length_read - first_part_length);
    logger->tail += length_read - first_part_length;

    return length_read;
}

void logger_free(Logger* logger) {
    free((void*) logger->buffer);
    free((void*) logger);
}
