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
#include <inttypes.h>

#include "logging_parser.h"

EmptyLog empty_log;

void logger_busy_read(Logger_Pool* logger, Byte* buffer, int length, AnalyzerType analyzer) {
    int bytes_read = 0;
    while (bytes_read < length) {
        bytes_read += logger_read(logger, buffer + bytes_read, length - bytes_read, analyzer);
    }
}


int next_log_type(Logger_Pool* logger) {
    int type;
    logger_busy_read(logger, (Byte*) &type, sizeof(type), RT_ANALYZER);
    return type;
}

char* timestamp_to_str(int64_t cur_ts, char *buf) {
    struct tm ts;
    time_t cur_ts_secs = (time_t)(cur_ts / 1000000);
    int64_t cur_ts_usecs = cur_ts % 1000000;
    char temp_buf[TIME_STAMP_LEN];

    if (localtime_r(&cur_ts_secs, &ts) == NULL) {
        snprintf(buf, TIME_STAMP_LEN, "INVALID_TIMESTAMP");
        return buf;
    }

    size_t len = strftime(temp_buf, TIME_STAMP_LEN - 8, LOG_NAME_PATTERN, &ts);
    if (len == 0) {
        snprintf(buf, TIME_STAMP_LEN, "FORMAT_ERROR");
        return buf;
    }

    snprintf(buf, TIME_STAMP_LEN, "%s.%06" PRId64, temp_buf, cur_ts_usecs);

    return buf;
}

void add_time_to_json_object(struct json_object *jobj, int64_t cur_ts)
{
    char time_buf[TIME_STAMP_LEN];
    json_object_object_add(jobj, "logging_time", json_object_new_string(timestamp_to_str(cur_ts, time_buf)));
}

/**
 * Json serializing functions for the various log types
 */

/**
 * writes a physical cell read log in json format to a given string
 * @param src the struct containing all the data to be added to the json
 * @param dst the pointer to the written string
 */
void JSON_PHYSICAL_CELL_READ(PhysicalCellReadLog *src, char ** dst)
{
    struct json_object *jobj;

    jobj = json_object_new_object();
    json_object_object_add(jobj, "type", json_object_new_string("PhysicalCellReadLog"));
    json_object_object_add(jobj, "channel", json_object_new_int(src->channel));
    json_object_object_add(jobj, "block", json_object_new_int(src->block));
    json_object_object_add(jobj, "page", json_object_new_int(src->page));
    add_time_to_json_object(jobj, src->metadata.logging_start_time);

    const char *json_string = json_object_to_json_string_ext(jobj, JSON_C_TO_STRING_SPACED);

    size_t json_length = strlen(json_string);
    *dst = (char *)malloc(json_length + 2);

    strcpy(*dst, json_string);
    strcat(*dst, "\n");
    json_object_put(jobj); // Delete the json object
}

/**
 * writes a physical cell program log in json format to a given string
 * @param src the struct containing all the data to be added to the json
 * @param dst the pointer to the written string
 */
void JSON_PHYSICAL_CELL_PROGRAM(PhysicalCellProgramLog *src, char ** dst)
{
    struct json_object *jobj;

    jobj = json_object_new_object();
    json_object_object_add(jobj, "type", json_object_new_string("PhysicalCellProgramLog"));
    json_object_object_add(jobj, "channel", json_object_new_int(src->channel));
    json_object_object_add(jobj, "block", json_object_new_int(src->block));
    json_object_object_add(jobj, "page", json_object_new_int(src->page));
    add_time_to_json_object(jobj, src->metadata.logging_start_time);

    const char *json_string = json_object_to_json_string_ext(jobj, JSON_C_TO_STRING_SPACED);

    size_t json_length = strlen(json_string);
    *dst = (char *)malloc(json_length + 2);

    strcpy(*dst, json_string);
    strcat(*dst, "\n");
    json_object_put(jobj); // Delete the json object
}

/**
 * writes a physical cell program compatible log in json format to a given string
 * @param src the struct containing all the data to be added to the json
 * @param dst the pointer to the written string
 */
void JSON_PHYSICAL_CELL_PROGRAM_COMPATIBLE(PhysicalCellProgramCompatibleLog *src, char ** dst)
{
    struct json_object *jobj;

    jobj = json_object_new_object();
    json_object_object_add(jobj, "type", json_object_new_string("PhysicalCellProgramCompatibleLog"));
    json_object_object_add(jobj, "channel", json_object_new_int(src->channel));
    json_object_object_add(jobj, "block", json_object_new_int(src->block));
    json_object_object_add(jobj, "page", json_object_new_int(src->page));
    add_time_to_json_object(jobj, src->metadata.logging_start_time);

    const char *json_string = json_object_to_json_string_ext(jobj, JSON_C_TO_STRING_SPACED);

    size_t json_length = strlen(json_string);
    *dst = (char *)malloc(json_length + 2);

    strcpy(*dst, json_string);
    strcat(*dst, "\n");
    json_object_put(jobj); // Delete the json object
}

/**
 * writes a logical cell program log in json format to a given string
 * @param src the struct containing all the data to be added to the json
 * @param dst the pointer to the written string
 */
void JSON_LOGICAL_CELL_PROGRAM(LogicalCellProgramLog *src, char ** dst)
{
    struct json_object *jobj;

    jobj = json_object_new_object();
    json_object_object_add(jobj, "type", json_object_new_string("LogicalCellProgramLog"));
    json_object_object_add(jobj, "channel", json_object_new_int(src->channel));
    json_object_object_add(jobj, "block", json_object_new_int(src->block));
    json_object_object_add(jobj, "page", json_object_new_int(src->page));
    add_time_to_json_object(jobj, src->metadata.logging_start_time);

    const char *json_string = json_object_to_json_string_ext(jobj, JSON_C_TO_STRING_SPACED);

    size_t json_length = strlen(json_string);
    *dst = (char *)malloc(json_length + 2);

    strcpy(*dst, json_string);
    strcat(*dst, "\n");
    json_object_put(jobj); // Delete the json object
}

/**
 * writes a garbage collection log in json format to a given string
 * @param src the struct containing all the data to be added to the json
 * @param dst the pointer to the written string
 */
void JSON_GARBAGE_COLLECTION(GarbageCollectionLog *src, char ** dst)
{
    struct json_object *jobj;

    jobj = json_object_new_object();
    json_object_object_add(jobj, "type", json_object_new_string("GarbageCollectionLog"));
    //add_time_to_json_object(jobj, src->metadata.logging_start_time);

    const char *json_string = json_object_to_json_string_ext(jobj, JSON_C_TO_STRING_SPACED);

    size_t json_length = strlen(json_string);
    *dst = (char *)malloc(json_length + 2);

    strcpy(*dst, json_string);
    strcat(*dst, "\n");
    json_object_put(jobj); // Delete the json object

    (void) src;
}

/**
 * writes a register read read log in json format to a given string
 * @param src the struct containing all the data to be added to the json
 * @param dst the pointer to the written string
 */
void JSON_REGISTER_READ(RegisterReadLog *src, char ** dst)
{
    struct json_object *jobj;

    jobj = json_object_new_object();
    json_object_object_add(jobj, "type", json_object_new_string("RegisterReadLog"));
    json_object_object_add(jobj, "channel", json_object_new_int(src->channel));
    json_object_object_add(jobj, "die", json_object_new_int(src->die));
    json_object_object_add(jobj, "reg", json_object_new_int(src->reg));
    add_time_to_json_object(jobj, src->metadata.logging_start_time);

    const char *json_string = json_object_to_json_string_ext(jobj, JSON_C_TO_STRING_SPACED);

    size_t json_length = strlen(json_string);
    *dst = (char *)malloc(json_length + 2);

    strcpy(*dst, json_string);
    strcat(*dst, "\n");
    json_object_put(jobj); // Delete the json object
}

/**
 * writes a register write read log in json format to a given string
 * @param src the struct containing all the data to be added to the json
 * @param dst the pointer to the written string
 */
void JSON_REGISTER_WRITE(RegisterWriteLog *src, char ** dst)
{
    struct json_object *jobj;

    jobj = json_object_new_object();
    json_object_object_add(jobj, "type", json_object_new_string("RegisterWriteLog"));
    json_object_object_add(jobj, "channel", json_object_new_int(src->channel));
    json_object_object_add(jobj, "die", json_object_new_int(src->die));
    json_object_object_add(jobj, "reg", json_object_new_int(src->reg));
    add_time_to_json_object(jobj, src->metadata.logging_start_time);

    const char *json_string = json_object_to_json_string_ext(jobj, JSON_C_TO_STRING_SPACED);

    size_t json_length = strlen(json_string);
    *dst = (char *)malloc(json_length + 2);

    strcpy(*dst, json_string);
    strcat(*dst, "\n");
    json_object_put(jobj); // Delete the json object
}

/**
 * writes a block erase read log in json format to a given string
 * @param src the struct containing all the data to be added to the json
 * @param dst the pointer to the written string
 */
void JSON_BLOCK_ERASE(BlockEraseLog *src, char ** dst)
{
    struct json_object *jobj;

    jobj = json_object_new_object();
    json_object_object_add(jobj, "type", json_object_new_string("BlockEraseLog"));
    json_object_object_add(jobj, "channel", json_object_new_int(src->channel));
    json_object_object_add(jobj, "die", json_object_new_int(src->die));
    json_object_object_add(jobj, "block", json_object_new_int(src->block));
    json_object_object_add(jobj, "dirty_page_nb", json_object_new_int(src->dirty_page_nb));
    add_time_to_json_object(jobj, src->metadata.logging_start_time);

    const char *json_string = json_object_to_json_string_ext(jobj, JSON_C_TO_STRING_SPACED);

    size_t json_length = strlen(json_string);
    *dst = (char *)malloc(json_length + 2);

    strcpy(*dst, json_string);
    strcat(*dst, "\n");
    json_object_put(jobj); // Delete the json object
}

/**
 * writes a page copy back log in json format to a given string
 * @param src the struct containing all the data to be added to the json
 * @param dst the pointer to the written string
 */
void JSON_PAGE_COPYBACK(PageCopyBackLog *src, char ** dst)
{
    struct json_object *jobj;

    jobj = json_object_new_object();
    json_object_object_add(jobj, "type", json_object_new_string("BlockEraseLog"));
    json_object_object_add(jobj, "channel", json_object_new_int(src->channel));
    json_object_object_add(jobj, "die", json_object_new_int(src->die));
    json_object_object_add(jobj, "block", json_object_new_int(src->block));
    json_object_object_add(jobj, "source_page", json_object_new_int(src->source_page));
    json_object_object_add(jobj, "destination_page", json_object_new_int(src->destination_page));
    add_time_to_json_object(jobj, src->metadata.logging_start_time);

    const char *json_string = json_object_to_json_string_ext(jobj, JSON_C_TO_STRING_SPACED);

    size_t json_length = strlen(json_string);
    *dst = (char *)malloc(json_length + 2);

    strcpy(*dst, json_string);
    strcat(*dst, "\n");
    json_object_put(jobj); // Delete the json object
}

/**
 * writes a channel switch to read read log in json format to a given string
 * @param src the struct containing all the data to be added to the json
 * @param dst the pointer to the written string
 */
void JSON_CHANNEL_SWITCH_TO_READ(ChannelSwitchToReadLog *src, char ** dst)
{
    struct json_object *jobj;

    jobj = json_object_new_object();
    json_object_object_add(jobj, "type", json_object_new_string("ChannelSwitchToReadLog"));
    json_object_object_add(jobj, "channel", json_object_new_int(src->channel));
    add_time_to_json_object(jobj, src->metadata.logging_start_time);

    const char *json_string = json_object_to_json_string_ext(jobj, JSON_C_TO_STRING_SPACED);

    size_t json_length = strlen(json_string);
    *dst = (char *)malloc(json_length + 2);

    strcpy(*dst, json_string);
    strcat(*dst, "\n");
    json_object_put(jobj); // Delete the json object
}

/**
 * writes a channel switch to write log in json format to a given string
 * @param src the struct containing all the data to be added to the json
 * @param dst the pointer to the written string
 */
void JSON_CHANNEL_SWITCH_TO_WRITE(ChannelSwitchToWriteLog *src, char **dst)
{
    struct json_object *jobj;

    jobj = json_object_new_object();
    json_object_object_add(jobj, "type", json_object_new_string("ChannelSwitchToWriteLog"));
    json_object_object_add(jobj, "channel", json_object_new_int(src->channel));
    add_time_to_json_object(jobj, src->metadata.logging_start_time);

    const char *json_string = json_object_to_json_string_ext(jobj, JSON_C_TO_STRING_SPACED);

    size_t json_length = strlen(json_string);
    *dst = (char *)malloc(json_length + 2);

    strcpy(*dst, json_string);
    strcat(*dst, "\n");
    json_object_put(jobj); // Delete the json object
}

/**
 * writes a object add page log in json format to a given string
 * @param src the struct containing all the data to be added to the json
 * @param dst the pointer to the written string
 */
void JSON_OBJECT_ADD_PAGE(ObjectAddPageLog *src, char ** dst)
{
    struct json_object *jobj;

    jobj = json_object_new_object();
    json_object_object_add(jobj, "type", json_object_new_string("ObjectAddPageLog"));
    json_object_object_add(jobj, "object id", json_object_new_int(src->object_id));
    json_object_object_add(jobj, "channel", json_object_new_int(src->channel));
    json_object_object_add(jobj, "page", json_object_new_int(src->page));

    const char *json_string = json_object_to_json_string_ext(jobj, JSON_C_TO_STRING_SPACED);

    size_t json_length = strlen(json_string);
    *dst = (char *)malloc(json_length + 2);

    strcpy(*dst, json_string);
    strcat(*dst, "\n");
    json_object_put(jobj); // Delete the json object
}

/**
 * writes a object copyback log in json format to a given string
 * @param src the struct containing all the data to be added to the json
 * @param dst the pointer to the written string
 */
void JSON_OBJECT_COPYBACK(ObjectCopyback *src, char ** dst)
{
    struct json_object *jobj;

    jobj = json_object_new_object();
    json_object_object_add(jobj, "type", json_object_new_string("ObjectCopyback"));
    json_object_object_add(jobj, "channel", json_object_new_int(src->channel));
    json_object_object_add(jobj, "source", json_object_new_int(src->source));
    json_object_object_add(jobj, "block", json_object_new_int(src->block));
    json_object_object_add(jobj, "destination", json_object_new_int(src->destination));
    const char *json_string = json_object_to_json_string_ext(jobj, JSON_C_TO_STRING_SPACED);

    size_t json_length = strlen(json_string);
    *dst = (char *)malloc(json_length + 2);

    strcpy(*dst, json_string);
    strcat(*dst, "\n");
    json_object_put(jobj); // Delete the json object
}

/**
 * writes a object copyback log in json format to a given string
 * @param src the struct containing all the data to be added to the json
 * @param dst the pointer to the written string
 */
void JSON_LOG_SYNC(LoggeingServerSync *src, char ** dst)
{
    struct json_object *jobj;

    jobj = json_object_new_object();
    json_object_object_add(jobj, "type", json_object_new_string("LoggeingServerSync"));
    json_object_object_add(jobj, "log_id", json_object_new_int(src->log_id));
    const char *json_string = json_object_to_json_string_ext(jobj, JSON_C_TO_STRING_SPACED);

    size_t json_length = strlen(json_string);
    *dst = (char *)malloc(json_length + 2);

    strcpy(*dst, json_string);
    strcat(*dst, "\n");
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
    void CONCAT(NEXT_, CONCAT(name, _LOG))(Logger_Pool* logger, structure *buf, AnalyzerType analyzer) {   \
        logger_busy_read(logger, (Byte*) buf, sizeof(structure), analyzer);  \
    }
_LOGS_DEFINITIONS(_LOGS_READER_DEFINITION_APPLIER)
