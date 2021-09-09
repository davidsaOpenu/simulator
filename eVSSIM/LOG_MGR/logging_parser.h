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
#include <json.h>
#include <sys/time.h>

//the maxmimum length of a timestamp
#define TIME_STAMP_LEN    80
#define TEST_NAME_MAX    128
#define TEST_SUITE_MAX   128
#define TEST_UUID_MAX     64

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
 * Log meta data structure
 */
typedef struct {
    /**
     * Logging start time (microseconds since epoch)
     */
    int64_t logging_start_time;

    /**
     * Logging end time (microseconds since epoch)
     */
    int64_t logging_end_time;

    /**
     * Test name
     */
    char    test_name[TEST_NAME_MAX];

    /**
     * Test suite name
     */
    char    test_suite[TEST_SUITE_MAX];

    /**
     * Run UUID of test
     */
    char    test_uuid[TEST_UUID_MAX];

    /**
     * Total SSD size in bytes
     */
    uint64_t ssd_size_bytes;

    /**
     * Test start time (Real Time)
     */
    int64_t test_start_time_us;
} LogMetadata;

/**
 * Read bytes from the logger, while busy waiting when there are not enough data
 * @param logger the logger to read the data from
 * @param buffer the buffer to write the data to
 * @param length the number of bytes to read
 * @param analyzer the type of analyzer
 */
void logger_busy_read(Logger_Pool* logger, Byte* buffer, int length, AnalyzerType analyzer);

/**
 * Return the type of the next log in the logger
 * @param logger the logger to read the next log type from
 * @return the next log type in the logger
 */
int next_log_type(Logger_Pool* logger);

/**
 * returns the the time given as a string
 * @param cur_ts the time in us
 * @param buf the buffer to be returned
 */
char* timestamp_to_str(int64_t cur_ts, char *buf);

/**
 * adds logging_time property to a json file
 * @param jobj the json object
 * @param cur_ts the time to be added in us
 */
void add_time_to_json_object(struct json_object *jobj, int64_t cur_ts);

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
 * A log of a physical cell program compatible
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
} PhysicalCellProgramCompatibleLog;

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
     * The die number of the register read
     */
    unsigned int die;
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
     * The die number of the written register
     */
    unsigned int die;
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
     * The die number of the erased block
     */
    unsigned int die;
    /**
     * The block number of the erased block
     */
    unsigned int block;
    /**
     * number of dirty pages in block prior to erase
     */
    uint64_t dirty_page_nb;
    /**
     * Log metadata
     */
    LogMetadata metadata;
} BlockEraseLog;

/**
 * A log of a page copy back
 */
typedef struct {
    /**
     * The channel number of the erased block
     */
    unsigned int channel;
    /**
     * The die number of the erased block
     */
    unsigned int die;
    /**
     * The block number of the erased block
     */
    unsigned int block;
    /**
     * The page number of the cell read
     */
    uint64_t source_page;
    /**
     * The page number of the programmed cell
     */
    uint64_t destination_page;
    /**
     * Log metadata
     */
    LogMetadata metadata;
} PageCopyBackLog;

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

typedef struct{
    /**
     *    The id of the object that the page is added to
     */
    uint64_t object_id;
     /**
     * The channel number of the written register
     */
    unsigned int channel;
    /**
     * The die number of the written register
     */
    unsigned int die;
    /**
     * The page number of the added page
     */
    uint32_t page;
}ObjectAddPageLog;

typedef struct{
     /**
     * The channel number of the written register
     */
    unsigned int channel;
    /**
     * The page id of the source
     */
    uint32_t source;
    /**
     *the block number of the source page
     */
    uint32_t block;
    /**
     * The page id of the destination
     */
    uint32_t destination;
}ObjectCopyback;

/**
 * Used to sync tests with log server, usually placed aat end of test before assert, to indicate logging_server synced with all logging event to this point
 */
typedef struct{
    /**
     * Random id used to check if log server is synced with log event
     */
    uint64_t log_id;
}LoggeingServerSync;

/**
 * A log of SSD utilization measurement
 */
typedef struct {
    /**
     * Utilization percentage (0.0 to 1.0)
     */
    double utilization_percent;
    /**
     * Total number of pages in SSD
     */
    uint64_t total_pages;
    /**
     * Number of occupied pages
     */
    uint64_t occupied_pages;
    /**
     * Log metadata
     */
    LogMetadata metadata;
} SsdUtilizationLog;

/**
 * All the logs definitions; used to easily add more log types
 * Each line should contain a call to the applier, with the structure and name of the log
 * In order to add a new log type, one must only add a new line with the log definition here
 * @param APPLIER the macro which is going to be applied to all the log types
 */
#define _LOGS_DEFINITIONS(APPLIER)                          \
APPLIER(PhysicalCellReadLog, PHYSICAL_CELL_READ)            \
APPLIER(PhysicalCellProgramLog, PHYSICAL_CELL_PROGRAM)      \
APPLIER(PhysicalCellProgramCompatibleLog, PHYSICAL_CELL_PROGRAM_COMPATIBLE) \
APPLIER(LogicalCellProgramLog, LOGICAL_CELL_PROGRAM)        \
APPLIER(GarbageCollectionLog, GARBAGE_COLLECTION)           \
APPLIER(RegisterReadLog, REGISTER_READ)                     \
APPLIER(RegisterWriteLog, REGISTER_WRITE)                   \
APPLIER(BlockEraseLog, BLOCK_ERASE)                         \
APPLIER(PageCopyBackLog, PAGE_COPYBACK)                     \
APPLIER(ChannelSwitchToReadLog, CHANNEL_SWITCH_TO_READ)     \
APPLIER(ChannelSwitchToWriteLog, CHANNEL_SWITCH_TO_WRITE)   \
APPLIER(ObjectAddPageLog, OBJECT_ADD_PAGE)                  \
APPLIER(ObjectCopyback, OBJECT_COPYBACK)                    \
APPLIER(LoggeingServerSync, LOG_SYNC)                       \
APPLIER(SsdUtilizationLog, SSD_UTILIZATION)                 \

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
    void CONCAT(NEXT_, CONCAT(name, _LOG))(Logger_Pool* logger, structure *buf, AnalyzerType analyzer);

/**
 * The customized NEXT_X_LOG definitions, for type-safe logging
 */
_LOGS_DEFINITIONS(_LOGS_READER_DECLARATION_APPLIER)

/**
 * The json log applier; used to create a json serializing function for the different log types
 * @param structure the structure associated with the log type
 * @param name the name of the log type
 */
#define _LOGS_JSON_DECLARATION_APPLIER(structure, name)           \
    void CONCAT(JSON_, name)(structure *src, char **dst);

/**
 * The customized NEXT_X_LOG definitions, for type-safe logging
 */
_LOGS_DEFINITIONS(_LOGS_JSON_DECLARATION_APPLIER)

#endif
