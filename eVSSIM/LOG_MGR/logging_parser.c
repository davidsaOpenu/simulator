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

/**
 * Json serializing functions for the various log types
 */
void JSON_PHYSICAL_CELL_READ(PhysicalCellReadLog *src, char *dst)
{
    const char *fmt = "{ \"channel\": \"%d\", \"block\": \"%d\", \"page\": \"%d\" }";
    sprintf(dst, fmt, src->channel, src->block, src->page);
}

void JSON_PHYSICAL_CELL_PROGRAM(PhysicalCellProgramLog *src, char *dst)
{
    const char *fmt = "{ \"channel\": \"%d\", \"block\": \"%d\", \"page\": \"%d\" }";
    sprintf(dst, fmt, src->channel, src->block, src->page);
}

void JSON_LOGICAL_CELL_PROGRAM(LogicalCellProgramLog *src, char *dst)
{
    const char *fmt = "{ \"channel\": \"%d\", \"block\": \"%d\", \"page\": \"%d\" }";
    sprintf(dst, fmt, src->channel, src->block, src->page);
}

void JSON_GARBAGE_COLLECTION(GarbageCollectionLog *src, char *dst)
{
    dst[0] = 0;
}

void JSON_REGISTER_READ(RegisterReadLog *src, char *dst)
{
    const char *fmt = "{ \"channel\": \"%d\", \"block\": \"%d\", \"page\": \"%d\" }";
    sprintf(dst, fmt, src->channel, src->die, src->reg);
}

void JSON_REGISTER_WRITE(RegisterWriteLog *src, char *dst)
{
    const char *fmt = "{ \"channel\": \"%d\", \"die\": \"%d\", \"reg\": \"%d\" }";
    sprintf(dst, fmt, src->channel, src->die, src->reg);
}

void JSON_BLOCK_ERASE(BlockEraseLog *src, char *dst)
{
    const char *fmt = "{ \"channel\": \"%d\", \"die\": \"%d\", \"block\": \"%d\" }";
    sprintf(dst, fmt, src->channel, src->die, src->block);
}

void JSON_CHANNEL_SWITCH_TO_READ(ChannelSwitchToReadLog *src, char *dst)
{
    const char *fmt = "{ \"channel\": \"%d\" }";
    sprintf(dst, fmt, src->channel);
}

void JSON_CHANNEL_SWITCH_TO_WRITE(ChannelSwitchToWriteLog *src, char *dst)
{
    const char *fmt = "{ \"channel\": \"%d\" }";
    sprintf(dst, fmt, src->channel);
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
    void CONCAT(NEXT_, CONCAT(name, _LOG))(Logger_Pool* logger, char *buf) {   \
        structure res;                                              \
        logger_busy_read(logger, (Byte*) &res, sizeof(structure));  \
        CONCAT(JSON_, name)(&res, buf);                             \
    }
_LOGS_DEFINITIONS(_LOGS_READER_DEFINITION_APPLIER)