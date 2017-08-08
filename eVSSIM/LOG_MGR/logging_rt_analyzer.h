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

#ifndef __LOGGING_RT_ANALYZER_H__
#define __LOGGING_RT_ANALYZER_H__


#include "logging_parser.h"


/*
 * The statistics gathered from the simulator
 */
typedef struct {
    // the number of physical page written
    unsigned int write_count;
    // the writing speed in MB/s
    double write_speed;                             //TODO integrate stat
    // the number of physical page read
    unsigned int read_count;
    // the reading speed in MB/s
    double read_speed;                              //TODO integrate stat
    // the number of garbage collection done
    unsigned int garbage_collection_count;
    // the write amplification of the ssd
    double write_amplification;
    // the utilization of the ssd
    double utilization;                             //TODO integrate stat
} SSDStatistics;


/*
 * A monitor hook
 */
typedef void (*MonitorHook)(SSDStatistics stats);


/*
 * The real time log analyzer structure
 */
typedef struct {
    // the logger to analyze
    Logger* logger;
    // the hook to use when analyzing
    MonitorHook hook;
} RTLogAnalyzer;


/*
 * Create a new real time log analyzer and return it
 */
RTLogAnalyzer* rt_log_analyzer_init(Logger* logger);

/*
 * Subscribe to the log analyzer, and return the old hook the analyzer used
 */
MonitorHook rt_log_analyzer_subscribe(RTLogAnalyzer* analyzer, MonitorHook hook);

/*
 * Do the main loop of the analyzer given
 * Read `max_logs` logs and then return; if negative, run forever
 */
void rt_log_analyzer_loop(RTLogAnalyzer* analyzer, int max_logs);

/*
 * Free the analyzer, with or without freeing the logger
 */
void rt_log_analyzer_free(RTLogAnalyzer* analyzer, int free_logger);


#endif
