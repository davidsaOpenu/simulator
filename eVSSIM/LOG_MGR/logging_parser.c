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


void JSON_OBJECT_ADD_PAGE(ObjectAddPageLog *src, char *dst)
{
    struct json_object *jobj;

    jobj = json_object_new_object();
    json_object_object_add(jobj, "type", json_object_new_string("ObjectAddPageLog"));
    json_object_object_add(jobj, "object id", json_object_new_int(src->object_id));
    json_object_object_add(jobj, "channel", json_object_new_int(src->channel));
    json_object_object_add(jobj, "page", json_object_new_int(src->page));
    add_time_to_json_object(jobj, src->metadata.logging_start_time);

    strcpy(dst, json_object_to_json_string_ext(jobj, JSON_C_TO_STRING_SPACED));
    strcat(dst, "\n");
    json_object_put(jobj); // Delete the json object
}

void JSON_OBJECT_COPYBACK(ObjectCopyback *src, char *dst)
{
    struct json_object *jobj;

    jobj = json_object_new_object();
    json_object_object_add(jobj, "type", json_object_new_string("ObjectCopyback"));
    json_object_object_add(jobj, "channel", json_object_new_int(src->channel));
    json_object_object_add(jobj, "source", json_object_new_int(src->source));
    json_object_object_add(jobj, "block", json_object_new_int(src->block));
    json_object_object_add(jobj, "destination", json_object_new_int(src->destination));
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
    structure CONCAT(NEXT_, CONCAT(name, _LOG))(Logger_Pool* logger) {   \
        structure res;                                              \
        logger_busy_read(logger, (Byte*) &res, sizeof(structure));  \
        return res;                                                 \
    }
_LOGS_DEFINITIONS(_LOGS_READER_DEFINITION_APPLIER)
