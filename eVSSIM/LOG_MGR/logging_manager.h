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

#ifndef __LOGGING_MANAGER_H__
#define __LOGGING_MANAGER_H__

#include "common.h"
#include "logging_rt_analyzer.h"

/**
 * The maximum number of analyzers a LogManager can hold
 */
#define MAX_ANALYZERS 16

/**
 * The number of statistics slots in the handler
 */
#define HANDLER_SIZE 4

/**
 * the time that the loop should wait for interupts each cycle
 */
#define MANGER_LOOP_SLEEP 500

/**
 * A structure used by the RTLogAnalyzer, to store the data while the manager is running
 */
typedef struct {
    /**
     * The slots for use by the analyzer, while the manager is running
     */
    SSDStatistics slots[HANDLER_SIZE];
    /**
     * The number of the current slot in use
     */
    unsigned int current_slot;
} AnalyzerHandler;

/**
 * The log manager structure
 */
typedef struct {
    /**
     * The handlers to be used by the analyzers
     */
    AnalyzerHandler handlers[MAX_ANALYZERS];
    /**
     * The number of analyzers currently using the manager
     */
    unsigned int analyzers_count;
    /**
     * The hooks to call after each analyze
     */
    MonitorHook hooks[MAX_SUBSCRIBERS];
    /**
     * The ids to use for the hooks
     */
    void* hooks_ids[MAX_SUBSCRIBERS];
    /**
     * The number of hooks currently subscribed to the manager
     */
    unsigned int subscribers_count;
    /**
     * Whether the manager should stop looping ASAP
     */
    unsigned int exit_loop_flag : 1;
} LogManager;

/**
 * Create a new log manager
 * @return the newly created log manager
 */
LogManager* log_manager_init(void);

/**
 * Subscribe to the log manager
 * @param manager the manager to subscribe to
 * @param hook the new hook to use
 * @param uid a pointer to user defined data which will be sent to the hook
 * @return 0 if the subscription succeeded, nonzero otherwise
 */
int log_manager_subscribe(LogManager* manager, MonitorHook hook, void* uid);

/**
 * Add an analyzer to the log manager
 * @param manager the manager to add the analyzer to
 * @param analyzer the analyzer to add
 * @return 0 if the addition succeeded, nonzero otherwise
 */
int log_manager_add_analyzer(LogManager* manager, RTLogAnalyzer* analyzer);

/**
 * Run the logging manager in the current thread;
 * The same as `log_manager_loop(manager, -1)`
 * @param manager the manager to run
 */
typedef struct log_manager_run_args {
    uint8_t device_index;
    LogManager* manager;
} log_manager_run_args_t;
void* log_manager_run(void* args);

/**
 * Do the main loop of the manager given
 * @param manager the manager to run
 * @param max_loops the maximum number of loops to do; if negative, run forever
 */
void log_manager_loop(uint8_t device_index, LogManager* manager, int max_loops);

/**
 * Free the manager
 * @param manager the manager to free
 */
void log_manager_free(LogManager* manager);

#endif
