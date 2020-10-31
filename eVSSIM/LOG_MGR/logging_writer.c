/*
 * Copyright 2020 The Open University of Israel
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

#include "logging_writer.h"
#include "logging_backend.h"
#include "common.h"

#include <errno.h>

static LoggerWriter *writer_obj;
static uint32_t file_index;

static logging_writer_retval logging_writer_open_file_for_write(void) {
    logging_writer_retval retval = LOGGING_WRITER_CRITICAL_ERROR;
    writer_obj->log_file = fopen(writer_obj->log_file_path, "w");

    if (NULL != writer_obj->log_file) {
        retval = LOGGING_WRITER_IS_OK;
    } else {
        PERR("Couldn't open file(%s) for writing!\n", writer_obj->log_file_path);
    }
    return retval;
}

static void logging_writer_close_file(void) {
    if (fclose(writer_obj->log_file) != 0) {
        PERR("Couldn't close file(%s)\n", writer_obj->log_file_path);
    }
}

void logging_writer_init(void) {
    LoggerWriter *obj = (LoggerWriter *) malloc(sizeof(LoggerWriter));

    if (NULL == obj) {
        PERR("Couldn't allocate memory for LoggerWriter, failed with error: %s\n", strerror(errno));
    }
    // int32_t ret = pthread_create(&obj->log_server_thread, NULL, logging_writer_main, NULL);

    // if (0 != ret) {
    //     PERR("Failed to create LoggerWriter thread!\n");
    //     logging_writer_free(obj);
    //     return NULL;
    // }

    sprintf(obj->log_file_path, "/tmp/log_file_%d.log", file_index); // TODO: Where should we get the file path from? ssd.conf?
    obj->log_file_size = 1024 * 1024; // TODO: Where should we get the file path from? ssd.conf?
    obj->running = true;
    file_index++;
    writer_obj = obj;
    PINFO("enter: logging_writer_init()\n");

    logging_writer_retval retval = logging_writer_open_file_for_write();

    if (LOGGING_WRITER_CRITICAL_ERROR == retval) {
        logging_writer_free(writer_obj);
    }
}

void logging_writer_free(LoggerWriter *obj) {
    if (NULL == obj) {
        PERR("Provided LoggerWriter object is NULL!\n");
    } else {
        obj->running = false;
        int32_t ret = pthread_join(obj->log_server_thread, NULL);

        if (0 != ret) {
            PERR("Failed to terminate LoggingWriter thread!\n");
        }
        logging_writer_close_file();
        free(obj);
    }
}

char *logging_writer_get_log_file_path(LoggerWriter *obj) {
    char *log_file_path = NULL;

    if (NULL != obj) {
        log_file_path = obj->log_file_path;
    }
    return log_file_path;
}

void logging_writer_save_log_to_file(Log *log_obj) {
    uint32_t buffer_size;

    Byte *buffer = logging_backend_read_log(log_obj, &buffer_size);

    if (NULL != buffer) {
        fwrite(buffer, sizeof(Byte), buffer_size, writer_obj->log_file);
    } else {
        PERR("Buffer is NULL!\n");
    }
    PINFO("enter: logging_writer_save_log_to_file()  buffer_size = %d\n", buffer_size);
    PINFO("log->tail = %p, log->buffer = (%p)\n", log_obj->tail, log_obj->buffer);
    // (LOG_SIZE - (log->tail - log->buffer))
}
