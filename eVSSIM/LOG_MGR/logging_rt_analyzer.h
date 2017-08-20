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


/**
 * The statistics gathered from the simulator
 */
typedef struct {
    /**
     * The number of physical page written
     */
    unsigned int write_count;
    /**
     * The writing speed in MB/s
     */
    double write_speed;
    /**
     * The number of physical page written
     */
    unsigned int read_count;
    /**
     * The reading speed in MB/s
     */
    double read_speed;
    /**
     * The number of garbage collection done
     */
    unsigned int garbage_collection_count;
    /**
     * The write amplification of the ssd
     */
    double write_amplification;
    /**
     * The utilization of the ssd
     */
    double utilization;
} SSDStatistics;


/**
 * Return a new, empty statistics structure
 * @return a new, empty statistics structure
 */
SSDStatistics stats_init(void);


int stats_json(SSDStatistics stats, Byte* buffer, int max_len);


/**
 * A monitor hook
 * @param stats the most recent statistics calculated
 * @param id a pointer to user defined data
 */
typedef void (*MonitorHook)(SSDStatistics stats, void* id);


/**
 * The maximum number of subscribers an RTLogAnalyzer can hold
 */
#define MAX_SUBSCRIBERS 5


/**
 * The real time log analyzer structure
 */
typedef struct {
    /**
     * The logger to analyzer
     */
    Logger* logger;
    /**
     * The hooks to call after each analyze
     */
    MonitorHook hooks[MAX_SUBSCRIBERS];
    /**
     * The ids to use for the hooks
     */
    void* hooks_ids[MAX_SUBSCRIBERS];
    /**
     * The number of hooks currently subscribed to the analyzer
     */
    unsigned int subscribers_count;
} RTLogAnalyzer;


/**
 * Create a new real time log analyzer
 * @param logger the logger to analyze
 * @return the newly created real time log analyzer
 */
RTLogAnalyzer* rt_log_analyzer_init(Logger* logger);

/**
 * Subscribe to the log analyzer
 * @param analyzer the analyzer to subscribe to
 * @param hook the new hook to use
 * @param id a pointer to user defined data which will be sent to the hook
 * @return 0 if the subscription succeeded, nonzero otherwise
 */
int rt_log_analyzer_subscribe(RTLogAnalyzer* analyzer, MonitorHook hook, void* id);

/**
 * Do the main loop of the analyzer given
 * @param analyzer the analyzer to run
 * @param max_logs the maximum number of logs to read; if negative, run forever
 */
void rt_log_analyzer_loop(RTLogAnalyzer* analyzer, int max_logs);

/**
 * Free the analyzer
 * @param analyzer the analyzer to free
 * @param free_logger whether to free the logger the analyzer uses
 */
void rt_log_analyzer_free(RTLogAnalyzer* analyzer, int free_logger);


#endif
