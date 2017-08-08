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
 */
void rt_log_analyzer_loop(RTLogAnalyzer* analyzer) {
    // allocate memory for the possible logs
    union {
        RegWriteLog reg_write_log;
        RegReadLog reg_read_log;
    } log_buffer;

    // run forever
    while (1) {
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
            switch (log_type) {
                case REG_WRITE_LOG_UID:
                    NEXT_LOG(analyzer->logger, log_buffer.reg_write_log);
                    break;
                case REG_READ_LOG_UID:
                    NEXT_LOG(analyzer->logger, log_buffer.reg_read_log);
                    break;
            }
            // call the right hook if present
            if (analyzer->hooks[log_type])
                analyzer->hooks[log_type]((void*) &log_buffer);
        }
    }
}
