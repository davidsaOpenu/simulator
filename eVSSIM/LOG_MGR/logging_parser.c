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
    structure CONCAT(NEXT_, CONCAT(name, _LOG))(Logger_Pool* logger) {   \
        structure res;                                              \
        logger_busy_read(logger, (void *)&res, sizeof(structure));  \
        return res;                                                 \
    }
_LOGS_DEFINITIONS(_LOGS_READER_DEFINITION_APPLIER)

unsigned int NEXT_CELL_PROGRAM_LOG_BLOCK_IDX(int blocks_number, Logger_Pool* logger) {

    unsigned int flash_nb;
    unsigned int block_nb;

    // skip PhysicalCellProgramLog.channel
    logger_busy_read(logger, NULL, sizeof(int));
    // read PhysicalCellProgramLog.flash
    logger_busy_read(logger, (void *)&flash_nb, sizeof(int));
    // read PhysicalCellProgramLog.block
    logger_busy_read(logger, (void *)&block_nb, sizeof(int));
    // skip PhysicalCellProgramLog.page
    logger_busy_read(logger, NULL, sizeof(int));
    // skip PhysicalCellProgramLog.metadata
    logger_busy_read(logger, NULL, sizeof(LogMetadata));

    return flash_nb * blocks_number + block_nb;
}

unsigned int NEXT_BLOCK_ERASE_LOG_BLOCK_IDX(int blocks_number, Logger_Pool* logger) {

    unsigned int flash_nb;
    unsigned int block_nb;

    // skip BlockEraseLog.channel
    logger_busy_read(logger, NULL, sizeof(int));
    // read BlockEraseLog.die
    logger_busy_read(logger, (void *)&flash_nb, sizeof(int));
    // read BlockEraseLog.block
    logger_busy_read(logger, (void *)&block_nb, sizeof(int));
    // skip left BlockEraseLog buffer
    logger_busy_read(logger, NULL, sizeof(BlockEraseLog) - sizeof(int) * 3);

    return flash_nb * blocks_number + block_nb;
}
