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

#include <stdlib.h>

#include "logging_rt_analyzer.h"


/*
 * Create a new real time log analyzer and return it
 */
RTLogAnalyzer* rt_log_analyzer_init(Logger* logger) {
    RTLogAnalyzer* analyzer = (RTLogAnalyzer*) malloc(sizeof(RTLogAnalyzer));
    if (analyzer == NULL)
        return NULL;
    analyzer->logger = logger;
    analyzer->hook = NULL;

    return analyzer;
}

/*
 * Subscribe to the log analyzer, and return the old hook the analyzer used
 */
MonitorHook rt_log_analyzer_subscribe(RTLogAnalyzer* analyzer, MonitorHook hook) {
    MonitorHook old_hook = analyzer->hook;
    analyzer->hook = hook;
    return old_hook;
}

/*
 * Do the main loop of the analyzer given
 * Read `max_logs` logs and then return; if negative, run forever
 */
void rt_log_analyzer_loop(RTLogAnalyzer* analyzer, int max_logs) {
    // init the statistics
    SSDStatistics stats = {
        .write_count = 0,
        .write_speed = 0.0,
        .read_count = 0,
        .read_speed = 0.0,
        .garbage_collection_count = 0,
        .write_amplification = 0.0,
        .utilization = 0.0
    };

    // additional variables needed to calculate the statistics
    unsigned int logical_write_count = 0;

    // run as long as necessary
    int logs_read = 0;
    while (max_logs < 0 || logs_read < max_logs) {
        int log_type = next_log_type(analyzer->logger);
        // if the log type is invalid continue
        if (log_type < 0 || log_type >= LOG_UID_COUNT || log_type == ERROR_LOG_UID)
            continue;

        // update the statistics according to the log
        switch (log_type) {
            case PHYSICAL_PAGE_READ_LOG_UID:
                NEXT_PHYSICAL_PAGE_READ_LOG(analyzer->logger);
                stats.read_count++;
                break;
            case PHYSICAL_PAGE_WRITE_LOG_UID:
                NEXT_PHYSICAL_PAGE_WRITE_LOG(analyzer->logger);
                stats.write_count++;
                break;
            case LOGICAL_PAGE_WRITE_LOG_UID:
                NEXT_LOGICAL_PAGE_WRITE_LOG(analyzer->logger);
                logical_write_count++;
                break;
            case GARBAGE_COLLECTION_LOG_UID:
                NEXT_GARBAGE_COLLECTION_LOG(analyzer->logger);
                stats.garbage_collection_count++;
                break;
        }
        if (logical_write_count == 0)
            stats.write_amplification = 0.0;
        else
            stats.write_amplification = ((double) stats.write_count) / logical_write_count;

        // call the hook if present
        if (analyzer->hook)
            analyzer->hook(stats);

        // update `logs_read` only if necessary (in order to avoid overflow)
        if (max_logs >= 0)
            logs_read++;
    }
}

/*
 * Free the analyzer, with or without freeing the logger
 */
void rt_log_analyzer_free(RTLogAnalyzer* analyzer, int free_logger) {
    if (free_logger)
        logger_free(analyzer->logger);
    free((void*) analyzer);
}
