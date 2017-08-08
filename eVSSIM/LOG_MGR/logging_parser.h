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

#ifndef __LOGGING_PARSER_H__
#define __LOGGING_PARSER_H__


#include "logging_backend.h"


/*
 * Macros to enable concatenation of names
 */
#define _CONCAT(x, y) x ## y
#define CONCAT(x, y) _CONCAT(x, y)


/*
 * Read `length` bytes from the logger to the buffer, while busy waiting when there are not enough data
 */
void logger_busy_read(Logger* logger, Byte* buffer, int length);


/*
 * A log of a register write
 */
typedef struct {
    // the register written to
    int reg;
} RegWriteLog;
/*
 * A log of a register read
 */
typedef struct {
    // the register read from
    int reg;
} RegReadLog;
/*...*/


/*
 * All the logs definitions; used to easily add more log types
 * Each line should contain a call to the applier, with the structure and name of the log
 * In order to add a new log type, one must only add a new line with the log definition here
 */
#define _LOGS_DEFINITIONS(APPLIER) \
    APPLIER(RegWriteLog, REG_WRITE) \
    APPLIER(RegReadLog, REG_READ)


/*
 * The enum log applier; used to create an enum of the log types' ids
 */
#define _LOGS_ENUM_APPLIER(structure, name) CONCAT(name, _LOG_UID),
/*
 * The unique ids for the different structs available for parsing
 */
enum ParsableStructures {
    ERROR_LOG_UID, /* Dummy used to detect errors in the log's id */
    _LOGS_DEFINITIONS(_LOGS_ENUM_APPLIER)
    LOG_UID_COUNT /* The number of different id's available; LEAVE AS LAST ITEM! */
};


/*
 * The writer log applier; used to create a writer function for the different log types
 */
#define _LOGS_WRITER_APPLYER(structure, name)                       \
    void CONCAT(LOG_, name)(Logger* logger, structure buffer) {     \
        int type = CONCAT(name, _LOG_UID);                          \
        logger_write(logger, (Byte*) &type, sizeof(type));          \
        logger_write(logger, (Byte*) &buffer, sizeof(structure));   \
    }
/*
 * The customized LOG_X functions, for type-safe logging
 */
_LOGS_DEFINITIONS(_LOGS_WRITER_APPLYER)


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


/*
 * The log union type applier; used to create a union type of all the structures of the log types
 */
#define _LOGS_UNION_TYPE_APPLIER(structure, name) structure name;
/*
 * A union of the structures corresponding with all the log types
 */
typedef union {
    _LOGS_DEFINITIONS(_LOGS_UNION_TYPE_APPLIER)
} UnionLogType;


/*
 * Return the type of the next log in the logger
 */
int next_log_type(Logger* logger);

/*
 * Write the next log in the logger to the struct
 * It is up to the user to make sure the struct is of the right type, using the log type
 * `logger` is a Logger* and `buffer` is a struct
 */
#define NEXT_LOG(logger, log_struct) \
    logger_busy_read(logger, (Byte*) &(log_struct), sizeof(log_struct))


#endif
