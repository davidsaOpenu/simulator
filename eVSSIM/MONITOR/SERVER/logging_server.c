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
#define WWW_DIR "./www"


/**
 * The logging server structure
 */
static struct {
    /**
     * A pointer to the lws context of the server
     */
    struct lws_context* context;
    /**
     * The lock of the server (used to safely update its statistics)
     */
    pthread_mutex_t lock;
    /**
     * The current statistics being displayed to the clients of the server
     */
    SSDStatistics stats;
} server;


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
 * @param in a pointer to the incoming message
 * @param len the length of the incoming message
 * @return zero if everything went OK, nonzero otherwise
 */
static int callback_http(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len) {
    // we do not care about requests which are not caught by the mount - not in the file system
    return 0;
}

/**
 * The callback used by LWS to handle the evssim-monitor protocol traffic
 * @param wsi a struct related to the client
 * @param reason the reason the callback was called
 * @param user a pointer to per-session data
 * @param in a pointer to the incoming message
 * @param len the length of the incoming message
 * @return zero if everything went OK, nonzero otherwise
 */
static int callback_evssim_monitor(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len) {
    WSSession* session = (WSSession*) user;

    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED:
            // on connection, init the per-session data nad schedule a write
            session->stats = stats_init();
            lws_callback_on_writable(wsi);
            break;
        case LWS_CALLBACK_SERVER_WRITEABLE:
            // on write schedule, copy the current statistics and send them
            pthread_mutex_lock(&server.lock);
            session->stats = server.stats;
            pthread_mutex_unlock(&server.lock);

            int len = stats_json(session->stats, &session->buffer[LWS_PRE], MAX_FRAME_SIZE);
            if (len < 0 || len > MAX_FRAME_SIZE)
                fprintf(stderr, "Couldn't write statistics to the client!");
            else
                lws_write(wsi, &session->buffer[LWS_PRE], len, LWS_WRITE_TEXT);
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
        0               // the maximum frame size sent by the protocol
    },
    {
        "evssim-monitor",
        callback_evssim_monitor,
        sizeof(WSSession),
        MAX_FRAME_SIZE
    },
    { NULL, NULL, 0, 0 }    // the array terminator
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
    if (pthread_mutex_init(&server.lock, NULL)) {
        lws_context_destroy(context);
        return 1;
    }

    // fill the members of the server structure
    server.context = context;
    server.stats = stats_init();

    return 0;
}

void log_server_update(SSDStatistics stats, void* _) {
    if (!stats_equal(server.stats, stats)) {
        pthread_mutex_lock(&server.lock);
        server.stats = stats;
        pthread_mutex_unlock(&server.lock);
        // schedule a write on all the clients connected to the evssim-monitor protocol
        lws_callback_on_writable_all_protocol(server.context, &ws_protocols[1]);
    }
}

void* log_server_run(void* _) {
    log_server_loop(-1);
    return NULL;
}

void log_server_loop(int max_loops) {
    int loops = 0;
    while (max_loops < 0 || loops < max_loops) {
        // process all waiting events and their callback functions, and then wait a bit
        lws_service(server.context, SERVER_WAIT_TIME);

        // update 'loops' only if necessary (in otder to avoid overflow)
        if (max_loops >= 0)
            loops++;
    }
}

void log_server_free(void) {
    lws_context_destroy(server.context);
    pthread_mutex_destroy(&server.lock);
}
