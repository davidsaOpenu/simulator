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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <json.h>

#include "logging_parser.h"

EmptyLog empty_log;

void logger_busy_read(Logger_Pool* logger, Byte* buffer, int length) {
    int bytes_read = 0;
    while (bytes_read < length) {
        bytes_read += logger_read(logger, buffer + bytes_read, length - bytes_read);
    }
}


int next_log_type(Logger_Pool* logger) {
    int type;
    logger_busy_read(logger, (Byte*) &type, sizeof(type));
    return type;
}

#define TIME_BUF_LEN (80)
char* timestamp_to_str(int64_t cur_ts, char *buf) {
    //setenv("TZ", "GMT+1", 1);
    //time_t now;
    struct tm  *ts;
    int64_t cur_ts_secs = cur_ts / 1000000;

    ts = localtime(&cur_ts_secs);

    strftime(buf, TIME_BUF_LEN, "%Y-%m-%d %H:%M:%S", ts);
    //printf("%s", buf);
    return buf;
}

#define TIME_FORMAT "\"logging_time\"  : \"%s\" "

/**
 * Json serializing functions for the various log types
 */
void JSON_PHYSICAL_CELL_READ(PhysicalCellReadLog *src, char *dst)
{
    char time_buf[TIME_BUF_LEN];
    const char *fmt = "{ \"type\": \"PhysicalCellReadLog\", \"channel\": %d, \"block\": %d, \"page\": %d, " TIME_FORMAT "}\n";
    sprintf(dst, fmt, src->channel, src->block, src->page, timestamp_to_str(src->metadata.logging_start_time,time_buf));
}

void JSON_PHYSICAL_CELL_PROGRAM(PhysicalCellProgramLog *src, char *dst)
{
    char time_buf[TIME_BUF_LEN];
    const char *fmt = "{ \"type\": \"PhysicalCellProgramLog\", \"channel\": %d, \"block\": %d, \"page\": %d , " TIME_FORMAT "}\n";
    sprintf(dst, fmt, src->channel, src->block, src->page, timestamp_to_str(src->metadata.logging_start_time,time_buf));
}

void JSON_LOGICAL_CELL_PROGRAM(LogicalCellProgramLog *src, char *dst)
{
    char time_buf[TIME_BUF_LEN];
    const char *fmt = "{ \"type\": \"LogicalCellProgramLog\", \"channel\": %d, \"block\": %d, \"page\": %d , " TIME_FORMAT "}\n";
    sprintf(dst, fmt, src->channel, src->block, src->page, timestamp_to_str(src->metadata.logging_start_time,time_buf));
}

void JSON_GARBAGE_COLLECTION(GarbageCollectionLog *src, char *dst)
{
    const char *fmt = "{ \"type\": \"GarbageCollectionLog\" }\n";
    strcpy(dst, fmt);
    (void) src;
}

void JSON_REGISTER_READ(RegisterReadLog *src, char *dst)
{
    char time_buf[TIME_BUF_LEN];
    const char *fmt = "{ \"type\": \"RegisterReadLog\", \"channel\": %d, \"block\": %d, \"page\": %d , " TIME_FORMAT "}\n";
    sprintf(dst, fmt, src->channel, src->die, src->reg, timestamp_to_str(src->metadata.logging_start_time,time_buf));
}

void JSON_REGISTER_WRITE(RegisterWriteLog *src, char *dst)
{
    char time_buf[TIME_BUF_LEN];
    const char *fmt = "{ \"type\": \"RegisterWriteLog\", \"channel\": %d, \"die\": %d, \"reg\": %d , " TIME_FORMAT "}\n";
    sprintf(dst, fmt, src->channel, src->die, src->reg, timestamp_to_str(src->metadata.logging_start_time,time_buf));
}

void JSON_BLOCK_ERASE(BlockEraseLog *src, char *dst)
{
    char time_buf[TIME_BUF_LEN];
    const char *fmt = "{ \"type\": \"BlockEraseLog\", \"channel\": %d, \"die\": %d, \"block\": %d , " TIME_FORMAT "}\n";
    sprintf(dst, fmt, src->channel, src->die, src->block, timestamp_to_str(src->metadata.logging_start_time,time_buf));
}

void JSON_CHANNEL_SWITCH_TO_READ(ChannelSwitchToReadLog *src, char *dst)
{
    char time_buf[TIME_BUF_LEN];
    const char *fmt = "{ \"type\": \"ChannelSwitchToReadLog\", \"channel\": %d , " TIME_FORMAT "}\n";
    sprintf(dst, fmt, src->channel, timestamp_to_str(src->metadata.logging_start_time,time_buf));
}

void JSON_CHANNEL_SWITCH_TO_WRITE(ChannelSwitchToWriteLog *src, char *dst)
{
    char time_buf[TIME_BUF_LEN];
    const char *fmt = "{ \"type\": \"ChannelSwitchToWriteLog\", \"channel\": %d , " TIME_FORMAT "}\n";
    sprintf(dst, fmt, src->channel, timestamp_to_str(src->metadata.logging_start_time,time_buf));
}

#define _LOGS_WRITER_DEFINITION_APPLIER(structure, name)            \
    void CONCAT(LOG_, name)(Logger_Pool* logger, structure buffer) {     \
        /*fprintf(stderr, "%p: " # name "\n", logger);*/            \
        if (logger == NULL)                                         \
            return;                                                 \
        int type = CONCAT(name, _LOG_UID);                          \
        logger_write(logger, (Byte*) &type, sizeof(type));          \
        logger_write(logger, (Byte*) &buffer, sizeof(structure));   \
    }
_LOGS_DEFINITIONS(_LOGS_WRITER_DEFINITION_APPLIER)


#define _LOGS_READER_DEFINITION_APPLIER(structure, name)            \
    void CONCAT(NEXT_, CONCAT(name, _LOG))(Logger_Pool* logger, structure *buf) {   \
        logger_busy_read(logger, (Byte*) buf, sizeof(structure));  \
    }
_LOGS_DEFINITIONS(_LOGS_READER_DEFINITION_APPLIER)