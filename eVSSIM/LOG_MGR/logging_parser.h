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
 * A log of a physical page read
 */
typedef struct {
    unsigned int channel;
    unsigned int block;
    unsigned int page;
} PhysicalPageReadLog;
/*
 * A log of a physical page write
 */
typedef struct {
    unsigned int channel;
    unsigned int block;
    unsigned int page;
} PhysicalPageWriteLog;
/*
 * A log of a logical page write
 */
typedef struct {
    unsigned int channel;
    unsigned int block;
    unsigned int page;
} LogicalPageWriteLog;
/*
 * A log of garbage collection
 */
typedef struct {

} GarbageCollectionLog;
/*...*/


/*
 * All the logs definitions; used to easily add more log types
 * Each line should contain a call to the applier, with the structure and name of the log
 * In order to add a new log type, one must only add a new line with the log definition here
 */
#define _LOGS_DEFINITIONS(APPLIER)  \
    APPLIER(PhysicalPageReadLog, PHYSICAL_PAGE_READ)    \
    APPLIER(PhysicalPageWriteLog, PHYSICAL_PAGE_WRITE)  \
    APPLIER(LogicalPageWriteLog, LOGICAL_PAGE_WRITE)    \
    APPLIER(GarbageCollectionLog, GARBAGE_COLLECTION)


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
#define _LOGS_WRITER_DECLARATION_APPLIER(structure, name)       \
    void CONCAT(LOG_, name)(Logger* logger, structure buffer);
/*
 * The customized LOG_X functions, for type-safe logging
 */
_LOGS_DEFINITIONS(_LOGS_WRITER_DECLARATION_APPLIER)


/*
 * The reader log applier; used to create a reader function for the different log types
 */
#define _LOGS_READER_DECLARATION_APPLIER(structure, name)           \
    structure CONCAT(NEXT_, CONCAT(name, _LOG))(Logger* logger);
/*
 * The customizes NEXT_X_LOG, for type-safe logging
 */
_LOGS_DEFINITIONS(_LOGS_READER_DECLARATION_APPLIER)


/*
 * Return the type of the next log in the logger
 */
int next_log_type(Logger* logger);


#endif
