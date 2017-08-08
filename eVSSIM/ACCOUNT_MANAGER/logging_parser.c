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

#include <math.h>
#include <string.h>

#include "logging_backend.h"
#include "logging_parser.h"


/*
 * Log the structure given (as a byte array)
 */
void log_struct(Logger* logger, unsigned char* struct_buffer, int length, int struct_type) {
    // write the type and then the structure itself
    logger_write(logger, (logger_data_t*) &struct_type, 1);
    logger_write(logger, (logger_data_t*) struct_buffer, ceil(length / (double) sizeof(logger_data_t)));
}

/*
 * Return the type of the next log in the logger
 */
int next_log_type(Logger* logger) {
    logger_data_t type;
    logger_read(logger, &type, 1);
    return (int) type;
}

/*
 * Write the next log in the logger to the buffer
 * It is up to the user to calculate `log_size` using the log type
 */
void next_log(Logger* logger, void* buffer, int log_size) {
    // first, write the part of the log that is stored in full units of `logger_data_t`
    int whole_effective_size = log_size / sizeof(logger_data_t);
    int leftover_size = log_size - (whole_effective_size * sizeof(logger_data_t));
    logger_read(logger, (logger_data_t*) buffer, whole_effective_size);
    // if we have some more left, copy it manually
    if (leftover_size > 0) {
        logger_data_t leftover;
        logger_read(logger, &leftover, 1);
        memcpy(buffer + whole_effective_size * sizeof(logger_data_t), (void*) &leftover, leftover_size);
    }
}
