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
 * A Byte
 */
typedef unsigned char Byte;


/*
 * The logger structure
 */
typedef struct {
    // the main buffer of the logger
    Byte* buffer;
    // the next place to insert a byte at
    Byte* head;
    // the next place to read a byte from
    Byte* tail;
    // the size of the buffer (including empty slots)
    size_t buffer_size;
} Logger;

/*
 * Create a new logger and return it
 */
Logger* logger_init(size_t size);

/*
 * Write `length` bytes from the buffer to the logger
 * If the logger can not fit all the data, print a warning to stderr
 */
void logger_write(Logger* logger, Byte* buffer, int length);

/*
 * Read at most `length` bytes from the logger to the buffer, and return the number of bytes read
 * If the logger does not have `length` bytes ready, busy wait until it has enough data
 */
int logger_read(Logger* logger, Byte* buffer, int length);

/*
 * Free the logger
 */
void logger_free(Logger* logger);


#endif
