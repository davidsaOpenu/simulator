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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "logging_offline_analyzer.h"

OfflineLogAnalyzer* offline_log_analyzer_init(Logger_Pool* logger_pool) {
    OfflineLogAnalyzer* analyzer = (OfflineLogAnalyzer*) malloc(sizeof(OfflineLogAnalyzer));
    if (analyzer == NULL)
        return NULL;

    analyzer->logger_pool = logger_pool;
    analyzer->exit_loop_flag = 0;

    return analyzer;
}

void* offline_log_analyzer_run(void* analyzer) {
    offline_log_analyzer_loop((OfflineLogAnalyzer*) analyzer);
    return NULL;
}

void offline_log_analyzer_loop(OfflineLogAnalyzer* analyzer) {
    Log *log, *temp_log;
    Byte buffer[LOG_SIZE];

    // loop as long as exit_loop_flag is not set
    while( ! ( analyzer->exit_loop_flag ) )
    {
        log = analyzer->logger_pool->dummy_log->next;
        while( !(analyzer->exit_loop_flag) && (log != analyzer->logger_pool->dummy_log))
        {
            temp_log = log->next;
            if(!(log->clean) && log->rt_analyzer_done)
            {
                logger_read_log(log, buffer, LOG_SIZE);
                // TODO
                // here we can do the magic of the offline analyzer
                // read the log
                // parse the data
                log->offline_analyzer_done = true;
            }
            log = temp_log;
        }

        logger_reduce_size(analyzer->logger_pool);
        logger_clean(analyzer->logger_pool);

        // go into penalty timeoff after each iteration of the analyzer loop
        // in order to let the rt analyzer complete it's operation on more logs
        (void)usleep(OFFLINE_ANALYZER_LOOP_TIMEOUT_US);
    }

    analyzer->exit_loop_flag = 0;
}

void offline_log_analyzer_free(OfflineLogAnalyzer* analyzer) {
    free((void*) analyzer);
}
