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


#define SERVER_WAIT_TIME 50
#define MAX_WS_FRAME_SIZE 512

#define LANDING_PAGE "./index.html"


static struct {
    struct lws_context* context;

    pthread_mutex_t stats_lock;
    SSDStatistics stats;
} server;


typedef struct {
    SSDStatistics stats;
    unsigned char buffer[MAX_WS_FRAME_SIZE + LWS_PRE];
} WSSession;


/* The callbacks for the http and ws protocols */


static int callback_http(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len) {
    switch (reason) {
        case LWS_CALLBACK_HTTP:
            lws_serve_http_file(wsi, LANDING_PAGE, "text/html", NULL, 0);
            break;
        default:
            break;
    }

    return 0;
}

static int callback_evssim_monitor(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len) {
    WSSession* session = (WSSession*) user;

    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED:
            // on connection, schedule a write
            session->stats = stats_init();
            lws_callback_on_writable(wsi);
            break;
        case LWS_CALLBACK_SERVER_WRITEABLE:
            // on write schedule, copy the current statistics and send them
            pthread_mutex_lock(&server.stats_lock);
            session->stats = server.stats;
            pthread_mutex_unlock(&server.stats_lock);

            int len = stats_json(session->stats, &session->buffer[LWS_PRE], MAX_WS_FRAME_SIZE);
            if (len < 0 || len > MAX_WS_FRAME_SIZE)
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


// list of supported protocols and callbacks
static struct lws_protocols ws_protocols[] = {
  // first protocol must always be HTTP handler
  {
    "http-only",    // name
    callback_http,  // callback
    0,              // per session data size
    0               // max frame size
  },
  {
    "evssim-monitor",
    callback_evssim_monitor,
    sizeof(WSSession),
    MAX_WS_FRAME_SIZE
  },
  {
    NULL, NULL, 0, 0
  }
};


static const struct lws_extension ws_extensions[] = {
    { NULL, NULL, NULL} // we support no web socket extensions
};


/* Wrapper methods */


int log_server_init(int port) {
    // init the creation info
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port = port;
    info.protocols = ws_protocols;
    info.extensions = ws_extensions;
    info.gid = -1;
    info.uid = -1;

    // try to create the context
    struct lws_context *context = lws_create_context(&info);
    if (context == NULL)
        return 1;

    // try to create the lock
    if (pthread_mutex_init(&server.stats_lock, NULL)) {
        lws_context_destroy(context);
        return 1;
    }

    // fill the members of the server structure
    server.context = context;
    server.stats = stats_init();

    return 0;
}

void log_server_update(SSDStatistics stats) {
    pthread_mutex_lock(&server.stats_lock);
    server.stats = stats;
    pthread_mutex_unlock(&server.stats_lock);
    // schedule a write on all the clients connected to the evssim-monitor protocol
    lws_callback_on_writable_all_protocol(server.context, &ws_protocols[1]);
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

void log_server_free() {
    lws_context_destroy(server.context);
    pthread_mutex_destroy(&server.stats_lock);
}
