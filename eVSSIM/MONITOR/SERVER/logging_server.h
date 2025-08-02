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

#ifndef __LOGGING_SERVER_H__
#define __LOGGING_SERVER_H__

#include <libwebsockets.h>

#include "logging_statistics.h"
#include "vssim_config_manager.h"
#include "logging_rt_analyzer.h"

#define MONITOR_SYNC_DELAY_USEC 15000000
#define DELAY_THRESHOLD 0
#define MONITOR_SYNC_DELAY(expected_duration) usleep(0 * expected_duration + MONITOR_SYNC_DELAY_USEC)

#define MONITOR_SLEEP_PERIOD_USEC 100
#define MONITOR_SLEEP_MAX_USEC 150000000
void MONITOR_SYNC_LOG_ID(SSDStatistics *stats, uint64_t log_id, uint64_t max_sleep);
void MONITOR_SYNC(SSDStatistics *stats, uint64_t max_sleep);

/**
 * The port to be used by the server
 */
#define LOG_SERVER_PORT ((g_used_upper_port) ? (2003 + MAX_DEVICES) : (2003 + (current_device_index)))
extern bool g_used_upper_port;

/**
 * A reset hook
 */
typedef void (*ResetHook)(void);


/**
 * The logging server structure
 */
typedef struct {
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
    /**
     * Whether the server should stop looping ASAP
     */
    unsigned int exit_loop_flag: 1;
    /**
     * The hook to call when a reset request is send by the user
     */
    ResetHook reset_hook;
} LogServer;

/**
 * The logging server structure declaration
 */
extern LogServer log_server;


/**
 * Initialize the logging server
 * @return 0 if initialization succeeded, nonzero otherwise
 */
int log_server_init(void);

/**
 * Update the statistics sent by the logging server
 * @param stats the new statistics to use
 */
void log_server_update(SSDStatistics stats);

/**
 * Update the current reset hook of the logging server
 * @param hook the new hook to use
 * @return the old reset hook
 */
ResetHook log_server_on_reset(ResetHook hook);

/**
 * Run the logging server in the current thread;
 * The same as `log_server_loop(-1)`
 * @param param - User provided parameter to thread function handler
 */
void* log_server_run(void *param);

/**
 * Do the main loop of the logging server
 * @param max_loops the maximum number of logs to read; if negative, run forever
 */
void log_server_loop(int max_loops);

/*
 * immidiatly terminate server, dont wait for next loop iterration 
 */
void log_server_stop(void);

/**
 * Free the data initialized for the logging server
 */
void log_server_free(void);

#endif