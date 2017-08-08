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

#include <stdio.h>
#include <string.h>

#include "logging_backend.h"


/*
 * Return the size of the logger (full slots only)
 * `logger` is a Logger*
 */
#define LOGGER_SIZE(logger) (((logger->head - logger->tail) + logger->buffer_size) % logger->buffer_size)

/*
 * Create a new logger and return it
 */
Logger* logger_init(size_t size) {
    // try to allocate the buffer (malloc is supposed to allocate memory which is aligned for primitive types)
    Byte* buffer = (Byte*) malloc(size);
    if (buffer == NULL)
        return NULL;

    // try to allocate the logger struct
    Logger* logger = (Logger*) malloc(sizeof(Logger));
    if (logger == NULL)
        return NULL;

    // update the fields and return the newly created logger
    logger->head = logger->tail = logger->buffer = buffer;
    logger->buffer_size = size;
    return logger;
}

/*
 * Write `length` bytes from the buffer to the logger
 * If the logger can not fit all the data, print a warning to stderr
 */
void logger_write(Logger* logger, Byte* buffer, int length) {
    // print a warning if the buffer is not empty enough
    if (logger->buffer_size - LOGGER_SIZE(logger) - 1 < length) { // one byte empty indicates a full buffer
        fprintf(stderr, "WARNING: the logger's buffer may have an overflow!");
        fprintf(stderr, "WARNING: the log may now be corrupted and unusable!");
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
}

/*
 * Read at most `length` bytes from the logger to the buffer, and return the number of bytes read
 * If the logger does not have `length` bytes ready, busy wait until it has enough data
 */
int logger_read(Logger* logger, Byte* buffer, int length) {
    // crop the maximum length
    int logger_size = LOGGER_SIZE(logger);
    if (logger_size < length)
        length = logger_size;

    // first, copy data until the end of the buffer of the logger
    int first_part_length = length;
    Byte* new_tail = logger->tail + length;
    if ((logger->tail - logger->buffer) + length >= logger->buffer_size) {
        first_part_length = logger->buffer_size - (logger->tail - logger->buffer);
        new_tail = logger->buffer;
    }
    memcpy((void*) buffer, (void*) logger->tail, first_part_length);
    logger->tail = new_tail;

    // next, copy the rest of the data (if any left)
    memcpy((void*) (buffer + first_part_length), (void*) logger->tail, length - first_part_length);
    logger->tail += length - first_part_length;

    return length;
}

/*
 * Free the logger
 */
void logger_free(Logger* logger) {
    free((void*) logger->buffer);
    free((void*) logger);
}
