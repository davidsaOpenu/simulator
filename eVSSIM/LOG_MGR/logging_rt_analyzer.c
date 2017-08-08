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
    int log_id;
    for (log_id = 0; log_id < LOG_UID_COUNT; log_id++)
        analyzer->hooks[log_id] = NULL;
    return analyzer;
}

/*
 * Subscribe to logs of type `log_uid` and return the old hook the analyzer used
 */
LogHook rt_log_analyzer_subscribe(RTLogAnalyzer* analyzer, int log_uid, LogHook hook) {
    LogHook old_hook = analyzer->hooks[log_uid];
    analyzer->hooks[log_uid] = hook;
    return old_hook;
}

/*
 * Do the main loop of the analyzer given
 * Read `max_logs` logs and then return; if negative, run forever
 */
void rt_log_analyzer_loop(RTLogAnalyzer* analyzer, int max_logs) {
    // allocate memory for the possible logs
    UnionLogType log_buffer;

    // run as long as necessary
    int logs_read = 0;
    while (max_logs < 0 || logs_read < max_logs) {
        int log_type = next_log_type(analyzer->logger);
        // if the log type is invalid
        if (log_type < 0 || log_type >= LOG_UID_COUNT) {
            // call the error hook if present
            if (analyzer->hooks[ERROR_LOG_UID])
                analyzer->hooks[ERROR_LOG_UID](NULL);
            continue;
        }
        // else, if the log type is valid
        else {
            // read the right log
            logger_busy_read(analyzer->logger, (Byte*) &log_buffer, LOG_SIZES[log_type]);
            // call the right hook if present
            if (analyzer->hooks[log_type])
                analyzer->hooks[log_type]((void*) &log_buffer);
        }

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
