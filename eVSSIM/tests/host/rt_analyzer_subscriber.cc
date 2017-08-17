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
    SSDStatistics results[] = {
            // register read
            {
                    .write_count = 0,
                    .write_speed = 0,
                    .read_count = 0,
                    .read_speed = 0,
                    .garbage_collection_count = 0,
                    .write_amplification = 0,
                    .utilization = 0
            },
            // physical cell read
            {
                    .write_count = 0,
                    .write_speed = 0,
                    .read_count = 1,
                    .read_speed = (1.0 / (82 + 50)) * 1000000.0,
                    .garbage_collection_count = 0,
                    .write_amplification = 0,
                    .utilization = 0
            },
            // channel switch to write
            {
                    .write_count = 0,
                    .write_speed = 0,
                    .read_count = 1,
                    .read_speed = (1.0 / (82 + 50)) * 1000000.0,
                    .garbage_collection_count = 0,
                    .write_amplification = 0,
                    .utilization = 0
            },
            // physical cell program
            {
                    .write_count = 1,
                    .write_speed = (1.0 / (33 + 900)) * 1000000.0,
                    .read_count = 1,
                    .read_speed = (1.0 / (82 + 50)) * 1000000.0,
                    .garbage_collection_count = 0,
                    .write_amplification = 0,
                    .utilization = 1.0 / (2.0 * 4.0 * 8.0)
            },
            // garbage collection
            {
                    .write_count = 1,
                    .write_speed = (1.0 / (33 + 900)) * 1000000.0,
                    .read_count = 1,
                    .read_speed = (1.0 / (82 + 50)) * 1000000.0,
                    .garbage_collection_count = 1,
                    .write_amplification = 0,
                    .utilization = 1.0 / (2.0 * 4.0 * 8.0)
            },
            // logical cell program
            {
                    .write_count = 1,
                    .write_speed = (1.0 / (33 + 900)) * 1000000.0,
                    .read_count = 1,
                    .read_speed = (1.0 / (82 + 50)) * 1000000.0,
                    .garbage_collection_count = 1,
                    .write_amplification = 1,
                    .utilization = 1.0 / (2.0 * 4.0 * 8.0)
            },
            // register write
            {
                    .write_count = 1,
                    .write_speed = (1.0 / (33 + 900)) * 1000000.0,
                    .read_count = 1,
                    .read_speed = (1.0 / (82 + 50)) * 1000000.0,
                    .garbage_collection_count = 1,
                    .write_amplification = 1,
                    .utilization = 1.0 / (2.0 * 4.0 * 8.0)
            },
            // physical cell program
            {
                    .write_count = 2,
                    .write_speed = (2.0 / (33 + 900 + 82 + 900)) * 1000000.0,
                    .read_count = 1,
                    .read_speed = (1.0 / (82 + 50)) * 1000000.0,
                    .garbage_collection_count = 1,
                    .write_amplification = 2.0,
                    .utilization = 2.0 / (2.0 * 4.0 * 8.0)
            },
            // logical cell program
            {
                    .write_count = 2,
                    .write_speed = (2.0 / (33 + 900 + 82 + 900)) * 1000000.0,
                    .read_count = 1,
                    .read_speed = (1.0 / (82 + 50)) * 1000000.0,
                    .garbage_collection_count = 1,
                    .write_amplification = 1.0,
                    .utilization = 2.0 / (2.0 * 4.0 * 8.0)
            },
            // block erase
            {
                    .write_count = 2,
                    .write_speed = (2.0 / (33 + 900 + 82 + 900)) * 1000000.0,
                    .read_count = 1,
                    .read_speed = (1.0 / (82 + 50)) * 1000000.0,
                    .garbage_collection_count = 1,
                    .write_amplification = 1.0,
                    .utilization = 0.0 / (2.0 * 4.0 * 8.0)
            },
            // channel switch to read
            {
                    .write_count = 2,
                    .write_speed = (2.0 / (33 + 900 + 82 + 900)) * 1000000.0,
                    .read_count = 1,
                    .read_speed = (1.0 / (82 + 50)) * 1000000.0,
                    .garbage_collection_count = 1,
                    .write_amplification = 1.0,
                    .utilization = 0.0 / (2.0 * 4.0 * 8.0)
            },
            // physical cell read
            {
                    .write_count = 2,
                    .write_speed = (2.0 / (33 + 900 + 82 + 900)) * 1000000.0,
                    .read_count = 2,
                    .read_speed = (2.0 / (82 + 50 + 2000 + 16 + 50)) * 1000000.0,
                    .garbage_collection_count = 1,
                    .write_amplification = 1.0,
                    .utilization = 0.0 / (2.0 * 4.0 * 8.0)
            },
            // garbage collection
            {
                    .write_count = 2,
                    .write_speed = (2.0 / (33 + 900 + 82 + 900)) * 1000000.0,
                    .read_count = 2,
                    .read_speed = (2.0 / (82 + 50 + 2000 + 16 + 50)) * 1000000.0,
                    .garbage_collection_count = 2,
                    .write_amplification = 1.0,
                    .utilization = 0.0 / (2.0 * 4.0 * 8.0)
            }
    };

    void write() {
        Logger* logger = _analyzer->logger;
        GarbageCollectionLog GC_LOG;
        LOG_REGISTER_READ(logger, (RegisterReadLog) { .channel = 19, .die = 20, .reg = 21});
        LOG_PHYSICAL_CELL_READ(logger, (PhysicalCellReadLog) { .channel = 1, .block = 2, .page = 3});
        LOG_CHANNEL_SWITCH_TO_WRITE(logger, (ChannelSwitchToWriteLog) { .channel = 28 });
        LOG_PHYSICAL_CELL_PROGRAM(logger, (PhysicalCellProgramLog) { .channel = 4, .block = 5, .page = 6});
        LOG_GARBAGE_COLLECTION(logger, GC_LOG);
        LOG_LOGICAL_CELL_PROGRAM(logger, (LogicalCellProgramLog) { .channel = 7, .block = 8, .page = 9});
        LOG_REGISTER_WRITE(logger, (RegisterWriteLog) {.channel = 22, .die = 23, .reg = 24});
        LOG_PHYSICAL_CELL_PROGRAM(logger, (PhysicalCellProgramLog) { .channel = 10, .block = 11, .page = 12});
        LOG_LOGICAL_CELL_PROGRAM(logger, (LogicalCellProgramLog) { .channel = 13, .block = 14, .page = 15});
        LOG_BLOCK_ERASE(logger, (BlockEraseLog) {.channel = 25, .die = 26, .block = 27});
        LOG_CHANNEL_SWITCH_TO_READ(logger, (ChannelSwitchToReadLog) { .channel = 29 });
        LOG_PHYSICAL_CELL_READ(logger, (PhysicalCellReadLog) { .channel = 16, .block = 17, .page = 18});
        LOG_GARBAGE_COLLECTION(logger, GC_LOG);
    }

    void read() {
        rt_log_analyzer_loop(_analyzer, 13);
    }

    /**
     * Assert that the statistics given are OK
     * @param stats the statistics to validate
     */
    void analyze_stats(SSDStatistics stats, void*) {
        SSDStatistics res = results[_logs_read];
        _logs_read++;

        ASSERT_EQ(res.write_count, stats.write_count);
        ASSERT_EQ(res.write_speed, stats.write_speed);
        ASSERT_EQ(res.read_count, stats.read_count);
        ASSERT_EQ(res.read_speed, stats.read_speed);
        ASSERT_EQ(res.garbage_collection_count, stats.garbage_collection_count);
        ASSERT_EQ(res.write_amplification, stats.write_amplification);
        ASSERT_EQ(res.utilization, stats.utilization);
    }

    void subscribe(RTLogAnalyzer* analyzer) {
        _logs_read = 0;
        _analyzer = analyzer;

        rt_log_analyzer_subscribe(_analyzer, (MonitorHook) analyze_stats, NULL);
    }
}
