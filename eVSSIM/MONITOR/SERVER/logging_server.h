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

#include "../../LOG_MGR/logging_rt_analyzer.h"


int log_server_init(int port);

void log_server_update(SSDStatistics stats);

void log_server_loop(int max_loops);

void log_server_free();


#endif
