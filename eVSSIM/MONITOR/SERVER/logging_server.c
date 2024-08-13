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

#include <pthread.h>
#include <string.h>

#include "logging_server.h"

/**
 * The time between server loops, in milliseconds
 */
#define SERVER_WAIT_TIME 50
/**
 * The maximum frame size, in bytes
 */
#define MAX_FRAME_SIZE 512
/**
 * The directory visible by the HTTP web server, relative to the working directory
 */
#ifdef VSSIM_NEXTGEN_BUILD_SYSTEM
    #define WWW_DIR "./vssim/simulator/www"
#else
    #define WWW_DIR "./www"
#endif


LogServer log_server;


/**
 * The session structure, used by each client in the server side
 */
typedef struct {
    /**
     * The current statistics being displayed to the client
     */
    SSDStatistics stats;
    /**
     * A buffer used to store the JSON before sending
     */
    unsigned char buffer[LWS_PRE + MAX_FRAME_SIZE];
} WSSession;


/* The callbacks for the http and ws protocols */


/**
 * The callback used by LWS to handle HTTP traffic which is not caught by the mount
 * @param wsi a struct related to the client
 * @param reason the reason the callback was called
 * @param user a pointer to per-session data
 * @param in_message a pointer to the incoming message
 * @param len the length of the incoming message
 * @return zero if everything went OK, nonzero otherwise
 */
static int callback_http(struct lws *wsi, enum lws_callback_reasons reason, void *user,
                         void *in_message, size_t len) {
    // we do not care about requests which are not caught by the mount - not in the file system
    (void) wsi; // Not Used Variable
    (void) reason; // Not Used Variable
    (void) user; // Not Used Variable
    (void) user; // Not Used Variable
    (void) in_message; // Not Used Variable
    (void) len; // Not Used Variable
    return 0;
}

/**
 * The callback used by LWS to handle the evssim-monitor protocol traffic
 * @param wsi a struct related to the client
 * @param reason the reason the callback was called
 * @param user a pointer to per-session data
 * @param in_message a pointer to the incoming message
 * @param len the length of the incoming message
 * @return zero if everything went OK, nonzero otherwise
 */
static int callback_evssim_monitor(struct lws *wsi, enum lws_callback_reasons reason, void *user,
                                   void *in_message, size_t len) {
    WSSession* session = (WSSession*) user;

    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED:
            // on connection, init the per-session data and schedule a write
            session->stats = stats_init();
            lws_callback_on_writable(wsi);
            break;
        case LWS_CALLBACK_SERVER_WRITEABLE:
            // on write schedule, copy the current statistics and send them
            pthread_mutex_lock(&log_server.lock);
            session->stats = log_server.stats;
            pthread_mutex_unlock(&log_server.lock);

            int json_len = stats_json(session->stats, &session->buffer[LWS_PRE], MAX_FRAME_SIZE);
            if (json_len < 0 || json_len > MAX_FRAME_SIZE)
                fprintf(stderr, "WARNING: Couldn't write statistics to the client!");
            else
                lws_write(wsi, &session->buffer[LWS_PRE], json_len, LWS_WRITE_TEXT);
            break;
        case LWS_CALLBACK_RECEIVE:
            // on receive, parse the command
            if (strcasecmp("reset", in_message) == 0) {
                if (log_server.reset_hook) {
                    log_server.reset_hook();
                }
                else {
                    fprintf(stderr, "WARNING: Couldn't reset stats, as no reset hook is configured!\n");
                    fprintf(stderr, "WARNING: Reseting local stats instead...\n");
                    log_server.stats = stats_init();
                    lws_callback_on_writable(wsi);
                }
            }
            else {
                fprintf(stderr, "WARNING: Unknown websocket command: %.*s\n", (int)((ssize_t) len),
                        (char*) in_message);
            }
            break;
        default:
            break;
    }

    return 0;
}


/* Helper structures for the server */


/**
 * An array of the protocols supported by the web server
 */
static struct lws_protocols ws_protocols[] = {
    // the first protocol must always be an HTTP handler
    {
        "http-only",    // the name of the protocol
        callback_http,  // the callback the protocol uses
        0,              // the size of the per-session data
        0,              // the maximum frame size sent by the protocol
        0,              // id, ignored by lws, here due to warning during compilation
        NULL,           // User, ignored by lws, here due to warning during compilation
        0               // tx_packet_size, 0 indicates restrict send() size to .rx_buffer_size for backwards- compatibility.
    },
    {
        "evssim-monitor",
        callback_evssim_monitor,
        sizeof(WSSession),
        MAX_FRAME_SIZE,
        0,
        NULL,
        0
    },
    { NULL, NULL, 0, 0, 0, NULL, 0}    // the array terminator
};

/**
 * An array of the web socket extensions supported by the web server
 */
static const struct lws_extension ws_extensions[] = {
    // we support no web socket extensions
{ NULL, NULL, NULL} // the array terminator
};

/**
 * A linked list of the mounts available on the server
 */
static const struct lws_http_mount http_mount = {
    NULL,           // pointer to the next item in the linked list (None here)
    "/",            // mount point in URL name space
    WWW_DIR,        // path to be mounted on
    "index.html",   // default target
    NULL,
    NULL,
    NULL,
    NULL,
    0,
    0,
    0,
    0,
    0,
    0,
    LWSMPRO_FILE,   // mount file system
    1,              // strlen("/")
    NULL,
    {NULL, NULL}
};


/* Wrapper methods */


int log_server_init(void) {
    // set debug level
    lws_set_log_level(LLL_ERR | LLL_WARN, NULL);

    // init the creation info
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port = LOG_SERVER_PORT;
    info.protocols = ws_protocols;
    info.extensions = ws_extensions;
    info.mounts = &http_mount;
    info.gid = -1;
    info.uid = -1;

    // try to create the context
    struct lws_context *context = lws_create_context(&info);
    if (context == NULL)
        return 1;

    // try to create the lock
    if (pthread_mutex_init(&log_server.lock, NULL)) {
        lws_context_destroy(context);
        return 1;
    }

    // fill the members of the server structure
    log_server.context = context;
    log_server.stats = stats_init();
    log_server.exit_loop_flag = 0;
    log_server.reset_hook = NULL;

    return 0;
}

void log_server_update(SSDStatistics stats) {
    if (!stats_equal(log_server.stats, stats)) {
        validateSSDStat(&stats);
        pthread_mutex_lock(&log_server.lock);
        log_server.stats = stats;
        pthread_mutex_unlock(&log_server.lock);
        // schedule a write on all the clients connected to the evssim-monitor protocol
        lws_callback_on_writable_all_protocol(log_server.context, &ws_protocols[1]);
    }
}

ResetHook log_server_on_reset(ResetHook hook) {
    ResetHook old_hook = log_server.reset_hook;
    log_server.reset_hook = hook;
    return old_hook;
}

void* log_server_run(void *param) {
    (void) param; // variable unused
    log_server_loop(-1);
    return NULL;
}

void log_server_loop(int max_loops) {
    int loops = 0;
    while (max_loops < 0 || loops < max_loops) {
        // process all waiting events and their callback functions, and then wait a bit
        lws_service(log_server.context, SERVER_WAIT_TIME);

        // update 'loops' only if necessary (in order to avoid overflow)
        if (max_loops >= 0)
            loops++;

        // exit if needed
        if (log_server.exit_loop_flag) {
            log_server.exit_loop_flag = 0;
            break;
        }
    }
}

void log_server_stop(void){
    lws_cancel_service(log_server.context);
}

void log_server_free(void) {
    lws_context_destroy(log_server.context);
    pthread_mutex_destroy(&log_server.lock);
}

void printSSDStat(SSDStatistics *stat){
    fprintf(stdout, "SSDStat:\n");
    fprintf(stdout, "\twrite_count = %u\n", stat->write_count);
    fprintf(stdout, "\twrite_speed = %f\n", stat->write_speed);
    fprintf(stdout, "\tread_count = %u\n", stat->read_count);
    fprintf(stdout, "\tread_speed = %f\n", stat->read_speed);
    fprintf(stdout, "\tgarbage_collection_count = %u\n", stat->garbage_collection_count);
    fprintf(stdout, "\twrite_amplification = %f\n", stat->write_amplification);
    fprintf(stdout, "\tutilization = %f\n", stat->utilization);  
};

void validateSSDStat(SSDStatistics *stat){
    bool hadError = false;
    if(stat->utilization < 0){
        fprintf(stderr, "bad utilization : %ff", stat->utilization);
        hadError = true;
    }
    
    //we don't cache writes so write amp cant be less then 1
    //2.2 was chosen as a 'good' upper limit for write amp. rben: updated to 10, there are test with lots of garbage collections, causing bad write amp, just an indication of bad ftl algorithem?
    if((stat->write_amplification < 1 && stat->write_amplification != 0) || stat->write_amplification > 10){
        if(stat->logical_write_count - 1 < stat->write_count){
            fprintf(stderr, "bad write_amplification : %ff but within margin\n", stat->write_amplification);
        }
        fprintf(stderr, "bad write_amplification : %ff\n", stat->write_amplification);
        hadError = true;
    }

    if(stat->write_speed < 0){
        fprintf(stderr, "bad write_speed : %ff\n", stat->write_speed);
        hadError = true;
    }
    if(stat->read_speed < 0){
        fprintf(stderr, "bad write_speed : %ff\n", stat->write_speed);
        hadError = true;
    }

    if(hadError){
        exit(2);
    }
}