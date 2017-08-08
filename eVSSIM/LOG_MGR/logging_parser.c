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
 * Return the type of the next log in the logger
 */
int next_log_type(Logger* logger) {
    int type;
    logger_busy_read(logger, (Byte*) &type, sizeof(type));
    return type;
}


/*
 * The writer log applier; used to create a writer function for the different log types
 */
#define _LOGS_WRITER_DEFINITION_APPLIER(structure, name)            \
    void CONCAT(LOG_, name)(Logger* logger, structure buffer) {     \
        int type = CONCAT(name, _LOG_UID);                          \
        logger_write(logger, (Byte*) &type, sizeof(type));          \
        logger_write(logger, (Byte*) &buffer, sizeof(structure));   \
    }
/*
 * The customized LOG_X functions, for type-safe logging
 */
_LOGS_DEFINITIONS(_LOGS_WRITER_DEFINITION_APPLIER)


/*
 * The reader log applier; used to create a reader function for the different log types
 */
#define _LOGS_READER_DEFINITION_APPLIER(structure, name)            \
    structure CONCAT(NEXT_, CONCAT(name, _LOG))(Logger* logger) {   \
        structure res;                                              \
        logger_busy_read(logger, (Byte*) &res, sizeof(structure));  \
        return res;                                                 \
    }
/*
 * The customizes NEXT_X_LOG, for type-safe logging
 */
_LOGS_DEFINITIONS(_LOGS_READER_DEFINITION_APPLIER)


/*
 * The log size applier; used to create array of the different log sizes
 */
#define _LOGS_SIZES_APPLIER(structure, name) sizeof(structure),
/*
 * The item at index X is the size of the struct corresponding with the log whose id is X
 */
const int LOG_SIZES[LOG_UID_COUNT] = {
    0, /* The size of the ERROR_LOG dummy */
    _LOGS_DEFINITIONS(_LOGS_SIZES_APPLIER)
};
