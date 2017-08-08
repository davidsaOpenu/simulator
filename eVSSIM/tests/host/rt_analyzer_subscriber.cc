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

extern "C" {

#include "common.h"
#include "logging_parser.h"
#include "logging_rt_analyzer.h"
}

#include <gtest/gtest.h>

#include <fstream>
#include <cstdio>
#include <cstdlib>


using namespace std;

namespace rt_subscriber {
    int _logs_read = 0;
    RTLogAnalyzer* _analyzer = NULL;

    /*
     * Write the different logs to the logger previously subscribed to
     */
    void write() {
        Logger* logger = _analyzer->logger;
        GarbageCollectionLog GC_LOG;
        LOG_PHYSICAL_PAGE_READ(logger, (PhysicalPageReadLog) { .channel = 1, .block = 2, .page = 3});
        LOG_PHYSICAL_PAGE_WRITE(logger, (PhysicalPageWriteLog) { .channel = 4, .block = 5, .page = 6});
        LOG_GARBAGE_COLLECTION(logger, GC_LOG);
        LOG_LOGICAL_PAGE_WRITE(logger, (LogicalPageWriteLog) { .channel = 7, .block = 8, .page = 9});
        LOG_PHYSICAL_PAGE_WRITE(logger, (PhysicalPageWriteLog) { .channel = 10, .block = 11, .page = 12});
        LOG_LOGICAL_PAGE_WRITE(logger, (LogicalPageWriteLog) { .channel = 13, .block = 14, .page = 15});
        LOG_PHYSICAL_PAGE_READ(logger, (PhysicalPageReadLog) { .channel = 16, .block = 17, .page = 18});
        LOG_GARBAGE_COLLECTION(logger, GC_LOG);
    }

    /*
     * Read the logs from the logger using rt_log_analyzer_loop
     */
    void read() {
        rt_log_analyzer_loop(_analyzer, 8);
    }

    /*
     * PhysicalPageReadLog hook
     */
    void analyze_physical_page_read(void* buffer) {
        PhysicalPageReadLog* log = (PhysicalPageReadLog*) buffer;
        if (_logs_read == 0) {
            ASSERT_EQ(log->channel, 1);
            ASSERT_EQ(log->block, 2);
            ASSERT_EQ(log->page, 3);
        }
        else if (_logs_read == 6) {
            ASSERT_EQ(log->channel, 16);
            ASSERT_EQ(log->block, 17);
            ASSERT_EQ(log->page, 18);
        }
        else {
            FAIL() << "unexpected physical page read log";
        }
        _logs_read++;
    }

    /*
     * PhysicalPageWriteLog hook
     */
    void analyze_physical_page_write(void* buffer) {
        PhysicalPageWriteLog* log = (PhysicalPageWriteLog*) buffer;
        if (_logs_read == 1) {
            ASSERT_EQ(log->channel, 4);
            ASSERT_EQ(log->block, 5);
            ASSERT_EQ(log->page, 6);
        }
        else if (_logs_read == 4) {
            ASSERT_EQ(log->channel, 10);
            ASSERT_EQ(log->block, 11);
            ASSERT_EQ(log->page, 12);
        }
        else {
            FAIL() << "unexpected physical page write logg";
        }
        _logs_read++;
    }

    /*
     * LogicalPageWriteLog hook
     */
    void analyze_logical_page_write(void* buffer) {
        LogicalPageWriteLog* log = (LogicalPageWriteLog*) buffer;
        if (_logs_read == 3) {
            ASSERT_EQ(log->channel, 7);
            ASSERT_EQ(log->block, 8);
            ASSERT_EQ(log->page, 9);
        }
        else if (_logs_read == 5) {
            ASSERT_EQ(log->channel, 13);
            ASSERT_EQ(log->block, 14);
            ASSERT_EQ(log->page, 15);
        }
        else {
            FAIL() << "unexpected logical page write log";
        }
        _logs_read++;
    }

    /*
     * GarbageCollectionLog hook
     */
    void analyze_garbage_collection(void* buffer) {
        if (_logs_read != 2 && _logs_read != 7)
            FAIL() << "unexpected garbage collection log";
        _logs_read++;
    }

    /*
     * Analyzer error hook
     */
    void analyzer_error(void* buffer) {
        FAIL() << "analyzer error";
    }

    /*
     * Subscribe to the analyzer given, and save it for future use
     */
    void subscribe(RTLogAnalyzer* analyzer) {
        _logs_read = 0;
        _analyzer = analyzer;

        rt_log_analyzer_subscribe(_analyzer, PHYSICAL_PAGE_READ_LOG_UID,
                                  (LogHook) analyze_physical_page_read);
        rt_log_analyzer_subscribe(_analyzer, PHYSICAL_PAGE_WRITE_LOG_UID,
                                  (LogHook) analyze_physical_page_write);
        rt_log_analyzer_subscribe(_analyzer, LOGICAL_PAGE_WRITE_LOG_UID,
                                  (LogHook) analyze_logical_page_write);
        rt_log_analyzer_subscribe(_analyzer, GARBAGE_COLLECTION_LOG_UID,
                                  (LogHook) analyze_garbage_collection);
        rt_log_analyzer_subscribe(_analyzer, ERROR_LOG_UID,
                                  (LogHook) analyzer_error);
    }
}
