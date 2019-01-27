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
#include <sys/time.h>

/**
 * Concatenate the parameters given
 * @param x the first parameter to concatenate
 * @param y the second parameter to concatenate
 * @return the concatenated parameters
 */
#define _CONCAT(x, y) x ## y
/**
 * Concatenate the parameters given
 * @param x the first parameter to concatenate
 * @param y the second parameter to concatenate
 * @return the concatenated parameters
 */
#define CONCAT(x, y) _CONCAT(x, y)

/**
 * Set current time in micro seconds in the given parameter
 * @param t the paramter that will contain the time
 */
#define TIME_MICROSEC(t) \
    int64_t t = 0; \
gettimeofday(&logging_parser_tv, NULL); \
t = logging_parser_tv.tv_sec; \
t *= 1000000; \
t += logging_parser_tv.tv_usec;

/**
 * Log meta data structer
 */
typedef struct {
    /**
     * Logging start time
     */
    int64_t logging_start_time;
    /**
     * Logging end time
     */
    int64_t logging_end_time;
} LogMetadata;

/**
 * Read bytes from the logger, while busy waiting when there are not enough data
 * @param logger the logger to read the data from
 * @param buffer the buffer to write the data to
 * @param length the number of bytes to read
 */
void logger_busy_read(Logger_Pool* logger, Byte* buffer, int length);

/**
 * Return the type of the next log in the logger
 * @param logger the logger to read the next log type from
 * @return the next log type in the logger
 */
int next_log_type(Logger_Pool* logger);

/**
 * A log which contains no attributes; should be an alias to every log which has no attributes
 */
typedef struct {

} EmptyLog;

/**
 * An empty log, to use in place of a log which contains no attributes
 */
extern EmptyLog empty_log;

/**
 * A log of a physical cell read
 */
typedef struct {
    /**
     * The channel number of the cell read
     */
    unsigned int channel;
    /**
     * The block number of the cell read
     */
    unsigned int block;
    /**
     * The page number of the cell read
     */
    unsigned int page;
    /**
     * Log metadata
     */
    LogMetadata metadata;
} PhysicalCellReadLog;

/**
 * A log of a physical cell program
 */
typedef struct {
    /**
     * The channel number of the programmed cell
     */
    unsigned int channel;
    /**
     * The block number of the programmed cell
     */
    unsigned int block;
    /**
     * The page number of the programmed cell
     */
    unsigned int page;
    /**
     * Log metadata
     */
    LogMetadata metadata;
} PhysicalCellProgramLog;

/**
 * A log of a logical cell program
 */
typedef struct {
    /**
     * The channel number of the programmed cell
     */
    unsigned int channel;
    /**
     * The block number of the programmed cell
     */
    unsigned int block;
    /**
     * The page number of the programmed cell
     */
    unsigned int page;
    /**
     * Log metadata
     */
    LogMetadata metadata;
} LogicalCellProgramLog;

/**
 * A log of garbage collection
 */
typedef EmptyLog GarbageCollectionLog;

/**
 * A log of a register read
 */
typedef struct {
    /**
     * The channel number of the register read
     */
    unsigned int channel;
    /**
     * The flash number of the register read
     */
    unsigned int flash;
    /**
     * The register number of the register read
     */
    unsigned int reg;
    /**
     * Log metadata
     */
    LogMetadata metadata;
} RegisterReadLog;

/**
 * A log of a register write
 */
typedef struct {
    /**
     * The channel number of the written register
     */
    unsigned int channel;
    /**
     * The flash number of the written register
     */
    unsigned int flash;
    /**
     * The register number of the written register
     */
    unsigned int reg;
    /**
     * Log metadata
     */
    LogMetadata metadata;
} RegisterWriteLog;

/**
 * A log of a block erase
 */
typedef struct {
    /**
     * The channel number of the erased block
     */
    unsigned int channel;
    /**
     * The flash number of the erased block
     */
    unsigned int flash;
    /**
     * The block number of the erased block
     */
    unsigned int block;
    /**
     * The number of written pages in block before erase
     */
    unsigned int erased_pages;
    /**
     * Log metadata
     */
    LogMetadata metadata;
} BlockEraseLog;

/**
 * A block of a channel switch to read mode
 */
typedef struct {
    /**
     * The channel number of the channel being switched
     */
    unsigned int channel;
    /**
     * Log metadata
     */
    LogMetadata metadata;
} ChannelSwitchToReadLog;

/**
 * A block of a channel switch to write mode
 */
typedef struct {
    /**
     * The channel number of the channel being switched
     */
    unsigned int channel;
    /**
     * Log metadata
     */
    LogMetadata metadata;
} ChannelSwitchToWriteLog;

/**
 * All the logs definitions; used to easily add more log types
 * Each line should contain a call to the applier, with the structure and name of the log
 * In order to add a new log type, one must only add a new line with the log definition here
 * @param APPLIER the macro which is going to be applied to all the log types
 */
#define _LOGS_DEFINITIONS(APPLIER)                          \
    APPLIER(PhysicalCellReadLog, PHYSICAL_CELL_READ)        \
APPLIER(PhysicalCellProgramLog, PHYSICAL_CELL_PROGRAM)      \
APPLIER(LogicalCellProgramLog, LOGICAL_CELL_PROGRAM)        \
APPLIER(GarbageCollectionLog, GARBAGE_COLLECTION)           \
APPLIER(RegisterReadLog, REGISTER_READ)                     \
APPLIER(RegisterWriteLog, REGISTER_WRITE)                   \
APPLIER(BlockEraseLog, BLOCK_ERASE)                         \
APPLIER(ChannelSwitchToReadLog, CHANNEL_SWITCH_TO_READ)     \
APPLIER(ChannelSwitchToWriteLog, CHANNEL_SWITCH_TO_WRITE)

/**
 * The enum log applier; used to create an enum of the log types' ids
 * @param structure the structure associated with the log type
 * @param name the name of the log type
 */
#define _LOGS_ENUM_APPLIER(structure, name) CONCAT(name, _LOG_UID),

/**
 * The unique ids for the different structs available for parsing
 */
enum ParsableStructures {
    /**
     * The definitions of the logs' ids of the different log types
     */
    _LOGS_DEFINITIONS(_LOGS_ENUM_APPLIER)
    /**
     * The number of different log types
     * LEAVE AS LAST ITEM!
     */
    LOG_UID_COUNT
};

/**
 * The writer log applier; used to create a writer function for the different log types
 * @param structure the structure associated with the log type
 * @param name the name of the log type
 */
#define _LOGS_WRITER_DECLARATION_APPLIER(structure, name)       \
    void CONCAT(LOG_, name)(Logger_Pool* logger, structure buffer);

/**
 * The customized LOG_X function definitions, for type-safe logging
 */
_LOGS_DEFINITIONS(_LOGS_WRITER_DECLARATION_APPLIER)

/**
 * The reader log applier; used to create a reader function for the different log types
 * @param structure the structure associated with the log type
 * @param name the name of the log type
 */
#define _LOGS_READER_DECLARATION_APPLIER(structure, name)           \
    structure CONCAT(NEXT_, CONCAT(name, _LOG))(Logger_Pool* logger);

/**
 * The customized NEXT_X_LOG definitions, for type-safe logging
 */
_LOGS_DEFINITIONS(_LOGS_READER_DECLARATION_APPLIER)

#endif
