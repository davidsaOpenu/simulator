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

    void write() {
        Logger* logger = _analyzer->logger;
        GarbageCollectionLog GC_LOG;
        LOG_PHYSICAL_CELL_READ(logger, (PhysicalCellReadLog) { .channel = 1, .block = 2, .page = 3});
        LOG_PHYSICAL_CELL_PROGRAM(logger, (PhysicalCellProgramLog) { .channel = 4, .block = 5, .page = 6});
        LOG_GARBAGE_COLLECTION(logger, GC_LOG);
        LOG_LOGICAL_CELL_PROGRAM(logger, (LogicalCellProgramLog) { .channel = 7, .block = 8, .page = 9});
        LOG_PHYSICAL_CELL_PROGRAM(logger, (PhysicalCellProgramLog) { .channel = 10, .block = 11, .page = 12});
        LOG_LOGICAL_CELL_PROGRAM(logger, (LogicalCellProgramLog) { .channel = 13, .block = 14, .page = 15});
        LOG_PHYSICAL_CELL_READ(logger, (PhysicalCellReadLog) { .channel = 16, .block = 17, .page = 18});
        LOG_GARBAGE_COLLECTION(logger, GC_LOG);
    }

    void read() {
        rt_log_analyzer_loop(_analyzer, 8);
    }

    /**
     * Assert that the statistics given are OK
     * @param stats the statistics to validate
     */
    void analyze_stats(SSDStatistics stats) {
        _logs_read++;

        switch (_logs_read) {
            case 1:
                // physical cell read
                ASSERT_EQ(0, stats.write_count);
                ASSERT_EQ(1, stats.read_count);
                ASSERT_EQ(0, stats.garbage_collection_count);
                ASSERT_EQ(0.0, stats.write_amplification);
                break;
            case 2:
                // physical cell program
                ASSERT_EQ(1, stats.write_count);
                ASSERT_EQ(1, stats.read_count);
                ASSERT_EQ(0, stats.garbage_collection_count);
                ASSERT_EQ(0.0, stats.write_amplification);
                break;
            case 3:
                // garbage collection
                ASSERT_EQ(1, stats.write_count);
                ASSERT_EQ(1, stats.read_count);
                ASSERT_EQ(1, stats.garbage_collection_count);
                ASSERT_EQ(0.0, stats.write_amplification);
                break;
            case 4:
                // logical cell program
                ASSERT_EQ(1, stats.write_count);
                ASSERT_EQ(1, stats.read_count);
                ASSERT_EQ(1, stats.garbage_collection_count);
                ASSERT_EQ(1.0, stats.write_amplification);
                break;
            case 5:
                // physical cell program
                ASSERT_EQ(2, stats.write_count);
                ASSERT_EQ(1, stats.read_count);
                ASSERT_EQ(1, stats.garbage_collection_count);
                ASSERT_EQ(2.0, stats.write_amplification);
                break;
            case 6:
                // logical cell program
                ASSERT_EQ(2, stats.write_count);
                ASSERT_EQ(1, stats.read_count);
                ASSERT_EQ(1, stats.garbage_collection_count);
                ASSERT_EQ(1.0, stats.write_amplification);
                break;
            case 7:
                // physical cell read
                ASSERT_EQ(2, stats.write_count);
                ASSERT_EQ(2, stats.read_count);
                ASSERT_EQ(1, stats.garbage_collection_count);
                ASSERT_EQ(1.0, stats.write_amplification);
                break;
            case 8:
                // garbage collection
                ASSERT_EQ(2, stats.write_count);
                ASSERT_EQ(2, stats.read_count);
                ASSERT_EQ(2, stats.garbage_collection_count);
                ASSERT_EQ(1.0, stats.write_amplification);
                break;
        }
    }

    void subscribe(RTLogAnalyzer* analyzer) {
        _logs_read = 0;
        _analyzer = analyzer;

        rt_log_analyzer_subscribe(_analyzer, (MonitorHook) analyze_stats);
    }
}
