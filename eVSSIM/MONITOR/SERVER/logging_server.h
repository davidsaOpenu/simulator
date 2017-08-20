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

#include "logging_rt_analyzer.h"


/**
 * The port to be used by the server
 */
#define LOG_SERVER_PORT 2003


/**
 * Initialize the logging server
 * @return 0 if initialization succeeded, nonzero otherwise
 */
int log_server_init(void);

/**
 * Update the statistics sent by the logging server
 * @param stats the new statistics to use
 */
void log_server_update(SSDStatistics stats, void*);

/**
 * Run the logging server in the current thread;
 * The same as `log_server_loop(-1)`
 */
void* log_server_run(void*);

/**
 * Do the main loop of the logging server
 * @param max_loops the maximum number of logs to read; if negative, run forever
 */
void log_server_loop(int max_loops);

/**
 * Free the data initialized for the logging server
 */
void log_server_free();


#endif
