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

/** Enqueue a node, insert at the back of the queue */
#define LOGGING_WRITER_ENQUEUE(node, dummy) { \
    node->next = dummy; \
    node->prev = dummy->prev; \
    node->next->prev = node; \
    node->prev->next = node; \
}

/** Dequeue from head of the queue one node, assign it to current_node */
#define LOGGING_WRITER_DEQUEUE(dummy, current_node) { \
    current_node = dummy->next; \
    dummy->next = dummy->next->next; \
}

static void logging_writer_save_queue_node_to_file(logger_writer_queue_node *queue_node) {
    PINFO("inside: logging_writer_save_queue_node_to_file()\n");
    if (NULL != queue_node) {
        Byte *buffer = logging_backend_read_log(queue_node->log_obj, &(queue_node->size));

        if (NULL != buffer) {
            fwrite(buffer, sizeof(Byte), queue_node->size, writer_obj->log_file);
        } else {
            PERR("Buffer is NULL!\n");
        }
    }
}

/**
 * @brief Handles queue messages. Gets queue message in FIFO mode, until the queue is empty.
 */
static void logging_writer_handle_messages_in_queue(void) {
    logger_writer_queue_node *queue_node;

    if (NULL != writer_obj && NULL != writer_obj->queue) {
        while (writer_obj->queue != writer_obj->queue->next) {
            LOGGING_WRITER_DEQUEUE(writer_obj->queue, queue_node);

            logging_writer_save_queue_node_to_file(queue_node);
        }
    } else {
        PERR("Failure on initialization!\n");
    }
}

/**
 * @brief Checks if the queue message queue is empty
 *
 * @return true On empty queue
 * @return false When queue is dirty
 */
static bool logging_writer_has_messages_in_queue(void) {
    PINFO("inside logging_writer_has_messages_in_queue\n");
    bool has_messages_in_queue = false;

    if (NULL != writer_obj && NULL != writer_obj->queue && writer_obj->queue->next != writer_obj->queue) {
        has_messages_in_queue = true;
    }
    PINFO("exiting logging_writer_has_messages_in_queue: %d\n", has_messages_in_queue);
    return has_messages_in_queue;
}

static void *logging_writer_main(void *param) {
    (void) param; // Not Used
    PINFO("inside logging_writer_main\n");

    if (NULL != writer_obj) {
        while(writer_obj->running) {
            while (logging_writer_has_messages_in_queue()) {
                logging_writer_handle_messages_in_queue();
            }
            /* When all messages were handled, go to sleep */
            pthread_mutex_lock(&writer_obj->lock);
        }
    } else {
        PERR("Writer Object is NULL!\n")
    }
    PINFO("Exiting Logging Writer main function\n");
    return NULL;
}

static logging_writer_retval logging_writer_open_file_for_write(void) {
    PINFO("enter: logging_writer_open_file_for_write\n");
    logging_writer_retval retval = LOGGING_WRITER_CRITICAL_ERROR;

    if (NULL != writer_obj) {
        writer_obj->log_file = fopen(writer_obj->log_file_path, "w");

        if (NULL != writer_obj->log_file) {
            retval = LOGGING_WRITER_IS_OK;
        } else {
            PERR("Couldn't open file(%s) for writing!\n", writer_obj->log_file_path);
        }
    } else {
        PERR("Writer Object is NULL!\n");
    }
    return retval;
}

static void logging_writer_close_file(void) {
    if (fclose(writer_obj->log_file) != 0) {
        PERR("Couldn't close file(%s)\n", writer_obj->log_file_path);
    }
}

void logging_writer_init(void) {
    PINFO("enter: logging_writer_init()\n");
    writer_obj = (LoggerWriter *) malloc(sizeof(LoggerWriter));

    if (NULL == writer_obj) {
        PERR("Couldn't allocate memory for LoggerWriter, failed with error: %s\n", strerror(errno));
    }

    /**
     * TOOD: this is ugly workaround for case:
     * When working locally, by running docker run qemu - we get logging_writer object creating a file for writing.
     * Then we run enter qemu to run log_mgr_tests (for example) - this will create another logging_writer that tries to open same file.
     */
    srand(time(0));
    int random_index = rand() % 100;

    sprintf(writer_obj->log_file_path, "/tmp/log_file_%d.log", random_index); // TODO: Where should we get the file path from? ssd.conf?
    writer_obj->log_file_size = 1024 * 1024; // TODO: Where should we get the file path from? ssd.conf?
    writer_obj->running = true;

    logging_writer_retval retval = logging_writer_open_file_for_write();

    if (LOGGING_WRITER_CRITICAL_ERROR == retval) {
        logging_writer_free();
        PERR("Failed to open file for logging writer!\n");
        return;
    }

    writer_obj->queue = (logger_writer_queue_node *) malloc(sizeof(logger_writer_queue_node));

    if (NULL == writer_obj->queue) {
        PERR("Couldn't allocate memory for logger_writer_queue, failed with error: %s\n", strerror(errno));
        return;
    }

    writer_obj->queue->next = writer_obj->queue;
    writer_obj->queue->prev = writer_obj->queue;

    writer_obj->running = true;
    // try to create the lock
    if (pthread_mutex_init(&writer_obj->lock, NULL) == 0) {
        int32_t ret = pthread_create(&writer_obj->log_server_thread, NULL, logging_writer_main, NULL);
        PINFO("created thread\n");

        if (0 != ret) {
            logging_writer_free();
            PERR("Failed to create LoggerWriter thread!\n");
            return;
        }
    }
}

void logging_writer_free(void) {
    PINFO("inside: logging_writer_free()\n");
    if (NULL == writer_obj) {
        PERR("Provided LoggerWriter object is NULL!\n");
    } else {
        writer_obj->running = false;
        pthread_mutex_unlock(&writer_obj->lock);
        int32_t ret = pthread_join(writer_obj->log_server_thread, NULL);

        if (0 != ret) {
            PERR("Failed to terminate LoggingWriter thread!\n");
        } else {
            pthread_mutex_destroy(&writer_obj->lock);
            logging_writer_close_file();
            free(writer_obj);
        }
    }
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
    PINFO("log_obj->tail = %p, log_obj->buffer = (%p)\n", log_obj->tail, log_obj->buffer);
}

void logging_writer_enqueue_log_for_write(Log *log_obj, uint32_t buffer_size) {
    PINFO("inside logging_writer_enqueue_log_for_write()\n");
    logger_writer_queue_node node;

    if (NULL != writer_obj && NULL != writer_obj->queue) {
        if (NULL != log_obj) {
            node.size = buffer_size;
            node.log_obj = log_obj;
            LOGGING_WRITER_ENQUEUE((&node), writer_obj->queue);
        }
    } else {
        PERR("Failure on initialization!\n");
    }
    PINFO("exiting logging_writer_enqueue_log_for_write()\n");
    pthread_mutex_unlock(&writer_obj->lock);
}
