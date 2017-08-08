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

#include <string.h>

#include "logging_parser.h"


/*
 * Read `length` bytes from the logger to the buffer, while busy waiting when there are not enough
 * data
 */
void logger_busy_read(Logger* logger, Byte* buffer, int length) {
    int bytes_read = 0;
    while (bytes_read < length) {
        bytes_read += logger_read(logger, buffer + bytes_read, length - bytes_read);
    }
}


/*
 * Log the structure given (as a byte array)
 */
void log_struct(Logger* logger, Byte* struct_buffer, int length, int struct_type) {
    // write the type and then the structure itself
    logger_write(logger, (Byte*) &struct_type, sizeof(struct_type));
    logger_write(logger, struct_buffer, length);
}

/*
 * Return the type of the next log in the logger
 */
int next_log_type(Logger* logger) {
    int type;
    logger_busy_read(logger, (Byte*) &type, sizeof(type));
    return type;
}
