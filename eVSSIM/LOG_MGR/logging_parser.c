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

void logger_busy_read(Logger_Pool* logger, Byte* buffer, int length, int rt_analyzer) {
    int bytes_read = 0;
    while (bytes_read < length) {
        bytes_read += logger_read(logger, buffer + bytes_read, length - bytes_read, rt_analyzer);
    }
}


int next_log_type(Logger_Pool* logger) {
    int type;
    logger_busy_read(logger, (Byte*) &type, sizeof(type), 1);
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

void add_time_to_json_object(struct json_object *jobj, int64_t cur_ts)
{
    char time_buf[TIME_BUF_LEN];
    json_object_object_add(jobj, "logging_time", json_object_new_string(timestamp_to_str(cur_ts, time_buf)));
}

/**
 * Json serializing functions for the various log types
 */
void JSON_PHYSICAL_CELL_READ(PhysicalCellReadLog *src, char *dst)
{
    struct json_object *jobj;

    jobj = json_object_new_object();
    json_object_object_add(jobj, "type", json_object_new_string("PhysicalCellReadLog"));
    json_object_object_add(jobj, "channel", json_object_new_int(src->channel));
    json_object_object_add(jobj, "block", json_object_new_int(src->block));
    json_object_object_add(jobj, "page", json_object_new_int(src->page));
    add_time_to_json_object(jobj, src->metadata.logging_start_time);

    strcpy(dst, json_object_to_json_string_ext(jobj, JSON_C_TO_STRING_SPACED));
    strcat(dst, "\n");
    json_object_put(jobj); // Delete the json object
}

void JSON_PHYSICAL_CELL_PROGRAM(PhysicalCellProgramLog *src, char *dst)
{
    struct json_object *jobj;

    jobj = json_object_new_object();
    json_object_object_add(jobj, "type", json_object_new_string("PhysicalCellProgramLog"));
    json_object_object_add(jobj, "channel", json_object_new_int(src->channel));
    json_object_object_add(jobj, "block", json_object_new_int(src->block));
    json_object_object_add(jobj, "page", json_object_new_int(src->page));
    add_time_to_json_object(jobj, src->metadata.logging_start_time);

    strcpy(dst, json_object_to_json_string_ext(jobj, JSON_C_TO_STRING_SPACED));
    strcat(dst, "\n");
    json_object_put(jobj); // Delete the json object
}

void JSON_LOGICAL_CELL_PROGRAM(LogicalCellProgramLog *src, char *dst)
{
    struct json_object *jobj;

    jobj = json_object_new_object();
    json_object_object_add(jobj, "type", json_object_new_string("LogicalCellProgramLog"));
    json_object_object_add(jobj, "channel", json_object_new_int(src->channel));
    json_object_object_add(jobj, "block", json_object_new_int(src->block));
    json_object_object_add(jobj, "page", json_object_new_int(src->page));
    add_time_to_json_object(jobj, src->metadata.logging_start_time);

    strcpy(dst, json_object_to_json_string_ext(jobj, JSON_C_TO_STRING_SPACED));
    strcat(dst, "\n");
    json_object_put(jobj); // Delete the json object
}

void JSON_GARBAGE_COLLECTION(GarbageCollectionLog *src, char *dst)
{
    struct json_object *jobj;

    jobj = json_object_new_object();
    json_object_object_add(jobj, "type", json_object_new_string("GarbageCollectionLog"));
    //add_time_to_json_object(jobj, src->metadata.logging_start_time);

    strcpy(dst, json_object_to_json_string_ext(jobj, JSON_C_TO_STRING_SPACED));
    strcat(dst, "\n");
    json_object_put(jobj); // Delete the json object

    (void) src;
}

void JSON_REGISTER_READ(RegisterReadLog *src, char *dst)
{
    struct json_object *jobj;

    jobj = json_object_new_object();
    json_object_object_add(jobj, "type", json_object_new_string("RegisterReadLog"));
    json_object_object_add(jobj, "channel", json_object_new_int(src->channel));
    json_object_object_add(jobj, "die", json_object_new_int(src->die));
    json_object_object_add(jobj, "reg", json_object_new_int(src->reg));
    add_time_to_json_object(jobj, src->metadata.logging_start_time);

    strcpy(dst, json_object_to_json_string_ext(jobj, JSON_C_TO_STRING_SPACED));
    strcat(dst, "\n");
    json_object_put(jobj); // Delete the json object
}

void JSON_REGISTER_WRITE(RegisterWriteLog *src, char *dst)
{
    struct json_object *jobj;

    jobj = json_object_new_object();
    json_object_object_add(jobj, "type", json_object_new_string("RegisterWriteLog"));
    json_object_object_add(jobj, "channel", json_object_new_int(src->channel));
    json_object_object_add(jobj, "die", json_object_new_int(src->die));
    json_object_object_add(jobj, "reg", json_object_new_int(src->reg));
    add_time_to_json_object(jobj, src->metadata.logging_start_time);

    strcpy(dst, json_object_to_json_string_ext(jobj, JSON_C_TO_STRING_SPACED));
    strcat(dst, "\n");
    json_object_put(jobj); // Delete the json object
}

void JSON_BLOCK_ERASE(BlockEraseLog *src, char *dst)
{
    struct json_object *jobj;

    jobj = json_object_new_object();
    json_object_object_add(jobj, "type", json_object_new_string("BlockEraseLog"));
    json_object_object_add(jobj, "channel", json_object_new_int(src->channel));
    json_object_object_add(jobj, "die", json_object_new_int(src->die));
    json_object_object_add(jobj, "block", json_object_new_int(src->block));
    add_time_to_json_object(jobj, src->metadata.logging_start_time);

    strcpy(dst, json_object_to_json_string_ext(jobj, JSON_C_TO_STRING_SPACED));
    strcat(dst, "\n");
    json_object_put(jobj); // Delete the json object
}

void JSON_CHANNEL_SWITCH_TO_READ(ChannelSwitchToReadLog *src, char *dst)
{
    struct json_object *jobj;

    jobj = json_object_new_object();
    json_object_object_add(jobj, "type", json_object_new_string("ChannelSwitchToReadLog"));
    json_object_object_add(jobj, "channel", json_object_new_int(src->channel));
    add_time_to_json_object(jobj, src->metadata.logging_start_time);

    strcpy(dst, json_object_to_json_string_ext(jobj, JSON_C_TO_STRING_SPACED));
    strcat(dst, "\n");
    json_object_put(jobj); // Delete the json object
}

void JSON_CHANNEL_SWITCH_TO_WRITE(ChannelSwitchToWriteLog *src, char *dst)
{
    struct json_object *jobj;

    jobj = json_object_new_object();
    json_object_object_add(jobj, "type", json_object_new_string("ChannelSwitchToWriteLog"));
    json_object_object_add(jobj, "channel", json_object_new_int(src->channel));
    add_time_to_json_object(jobj, src->metadata.logging_start_time);

    strcpy(dst, json_object_to_json_string_ext(jobj, JSON_C_TO_STRING_SPACED));
    strcat(dst, "\n");
    json_object_put(jobj); // Delete the json object
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
    void CONCAT(NEXT_, CONCAT(name, _LOG))(Logger_Pool* logger, structure *buf, int rt_analyzer) {   \
        logger_busy_read(logger, (Byte*) buf, sizeof(structure), rt_analyzer);  \
    }
_LOGS_DEFINITIONS(_LOGS_READER_DEFINITION_APPLIER)
