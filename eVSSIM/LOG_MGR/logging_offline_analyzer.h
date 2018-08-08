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

#ifndef __LOGGING_OFFLINE_ANALYZER_H__
#define __LOGGING_OFFLINE_ANALYZER_H__

#include "logging_parser.h"

/**
 * The offline log analyzer structure
 */
typedef struct {
    /**
     * The logger pool to analyze
     */
    Logger_Pool* logger_pool;
    /**
     * Whether the analyzer should stop looping ASAP
     */
    unsigned int exit_loop_flag : 1;
} OfflineLogAnalyzer;

/**
 * Create a new offline log analyzer
 * @param logger the logger pool to analyze
 * @return the newly created real time log analyzer
 */
OfflineLogAnalyzer* offline_log_analyzer_init(Logger_Pool* logger_pool);

/**
 * run the log analyzer
 * @param OfflineLogAnalyzer
 * @return NULL
 */
void* offline_log_analyzer_run(void* analyzer);

/**
 * Do the main loop of the offline analyzer
 */
void offline_log_analyzer_loop(OfflineLogAnalyzer* analyzer);

/**
 * Free the offline analyzer
 * @param analyzer the offline analyzer to free
 */
void offline_log_analyzer_free(OfflineLogAnalyzer* analyzer);

#endif
