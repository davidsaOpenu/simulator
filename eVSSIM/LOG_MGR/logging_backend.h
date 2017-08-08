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


/*
 * The backend logging mechanism is implemented using one long array in memory, which contains a
 * circular array inside; The array is aligned in memory, for a better caching performance
 * WARNING: the logging implemented here supports only one consumer and one producer!!
 */


/**
 * The alignment to use when allocating the logger's buffer
 */
#define LOGGER_BUFFER_ALIGNMENT 64


/**
 * A byte typedef, for easier use
 */
typedef unsigned char Byte;


/**
 * The logger structure
 */
typedef struct {
    /**
     * The main buffer of the logger
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
     * The size of the buffer (including empty slots)
     */
    size_t buffer_size;
} Logger;

/**
 * Create a new logger
 * @param size the size of the buffer to allocate for the logger
 *  Due to the way posix_memalign works, size must be a power of two multiple of sizeof(void *)
 * @return the newly created logger
 */
Logger* logger_init(size_t size);

/**
 * Write a byte array to the logger
 * Print a warning to stderr if the logger cannot fit all the data
 * @param logger the logger to write the bytes to
 * @param buffer the buffer to read the bytes from
 * @param length the number of bytes to copy from the buffer to the logger
 * @return 0 if the logger can fit all the data, otherwise nonzero
 */
int logger_write(Logger* logger, Byte* buffer, int length);

/**
 * Read a byte array from the logger
 * @param logger the logger to read the data from
 * @param buffer the buffer to write the data to
 * @param length the maximum number of bytes to read
 * @return the number of bytes read
 */
int logger_read(Logger* logger, Byte* buffer, int length);

/**
 * Free the logger
 * @param logger the logger to free
 */
void logger_free(Logger* logger);


#endif
