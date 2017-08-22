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

#ifndef __MONITOR_TEST_H__
#define __MONITOR_TEST_H__

#include "../../LOG_MGR/logging_statistics.h"


/**
 * Run the monitor in the current thread
 */
void* run_monitor(void*);

/**
 * Update the statistics displayed in the monitor
 * If no window is shown, block until one appears
 * @param stats the new statistics to display
 */
void update_stats(SSDStatistics stats, void*);


#endif
