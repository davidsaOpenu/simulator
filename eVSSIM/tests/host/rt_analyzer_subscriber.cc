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
}

#include <gtest/gtest.h>

#include <fstream>
#include <cstdio>
#include <cstdlib>


using namespace std;

/**
 * The rt subscriber does a regression test of the real time log analyzer.
 * It consists of three main calls:
 * - subscribe: subscribes to the analyzer given, and saves it.
 * - write: writes an artificial sequence of different logs to the logger associated with the analyzer.
 * - read: checks the output of the analyzer using the handler `analyze_stats`.
 * This test checks that the statistics that the analyzer outputs are valid, and corresponds to
 * the logs written during the `write` function. In addition, it makes sure that if the stats
 * do not change after a log was written to the logger, then the analyzer does not output any
 * stats (in order to reduce the amount of data flowing between the different parts of the
 * logging mechanism).
 * The expected output of the analyzer is stored in the variable `results`, and it is used in the
 * `analyze_stats` hook to compare the expected output and the actual output.
 */
namespace rt_subscriber {
    // delay constants as written to ssd.conf by log_mgr_tests.cc
    const static int REG_WRITE_DELAY = 82;
    const static int CELL_PROGRAM_DELAY = 900;
    const static int REG_READ_DELAY = 82;
    const static int CELL_READ_DELAY = 50;
    const static int BLOCK_ERASE_DELAY = 2000;
    const static int CHANNEL_SWITCH_DELAY_R = 16;
    const static int CHANNEL_SWITCH_DELAY_W = 33;

    // some more constants
    const static int SEC_IN_USEC = 1000000;
    const static int PAGES_IN_SSD = 2 * 8 * 4;

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
            // channel switch to write
            {
                    .write_count = 0,
                    .write_speed = 0,
                    .read_count = 1,
                    .read_speed = (1.0 / (REG_READ_DELAY + CELL_READ_DELAY)) * SEC_IN_USEC,
                    .garbage_collection_count = 0,
                    .write_amplification = 0,
                    .utilization = 0
            },
            // physical cell program
            {
                    .write_count = 1,
                    .write_speed = (1.0 / (CHANNEL_SWITCH_DELAY_W + CELL_PROGRAM_DELAY)) * SEC_IN_USEC,
                    .read_count = 1,
                    .read_speed = (1.0 / (REG_READ_DELAY + CELL_READ_DELAY)) * SEC_IN_USEC,
                    .garbage_collection_count = 0,
                    .write_amplification = 0,
                    .utilization = 1.0 / PAGES_IN_SSD
            },
            // garbage collection
            {
                    .write_count = 1,
                    .write_speed = (1.0 / (CHANNEL_SWITCH_DELAY_W + CELL_PROGRAM_DELAY)) * SEC_IN_USEC,
                    .read_count = 1,
                    .read_speed = (1.0 / (REG_READ_DELAY + CELL_READ_DELAY)) * SEC_IN_USEC,
                    .garbage_collection_count = 1,
                    .write_amplification = 0,
                    .utilization = 1.0 / PAGES_IN_SSD
            },
            // logical cell program
            // register write
            {
                    .write_count = 1,
                    .write_speed = (1.0 / (CHANNEL_SWITCH_DELAY_W + CELL_PROGRAM_DELAY)) * SEC_IN_USEC,
                    .read_count = 1,
                    .read_speed = (1.0 / (REG_READ_DELAY + CELL_READ_DELAY)) * SEC_IN_USEC,
                    .garbage_collection_count = 1,
                    .write_amplification = 1,
                    .utilization = 1.0 / PAGES_IN_SSD
            },
            // physical cell program
            {
                    .write_count = 2,
                    .write_speed = (2.0 / (CHANNEL_SWITCH_DELAY_W + CELL_PROGRAM_DELAY + REG_WRITE_DELAY + CELL_PROGRAM_DELAY)) * SEC_IN_USEC,
                    .read_count = 1,
                    .read_speed = (1.0 / (REG_READ_DELAY + CELL_READ_DELAY)) * SEC_IN_USEC,
                    .garbage_collection_count = 1,
                    .write_amplification = 2.0,
                    .utilization = 2.0 / PAGES_IN_SSD
            },
            // logical cell program
            {
                    .write_count = 2,
                    .write_speed = (2.0 / (CHANNEL_SWITCH_DELAY_W + CELL_PROGRAM_DELAY + REG_WRITE_DELAY + CELL_PROGRAM_DELAY)) * SEC_IN_USEC,
                    .read_count = 1,
                    .read_speed = (1.0 / (REG_READ_DELAY + CELL_READ_DELAY)) * SEC_IN_USEC,
                    .garbage_collection_count = 1,
                    .write_amplification = 1.0,
                    .utilization = 2.0 / PAGES_IN_SSD
            },
            // block erase
            // channel switch to read
            {
                    .write_count = 2,
                    .write_speed = (2.0 / (CHANNEL_SWITCH_DELAY_W + CELL_PROGRAM_DELAY + REG_WRITE_DELAY + CELL_PROGRAM_DELAY)) * SEC_IN_USEC,
                    .read_count = 1,
                    .read_speed = (1.0 / (REG_READ_DELAY + CELL_READ_DELAY)) * SEC_IN_USEC,
                    .garbage_collection_count = 1,
                    .write_amplification = 1.0,
                    .utilization = 1.0 / PAGES_IN_SSD
            },
            // physical cell read
            {
                    .write_count = 2,
                    .write_speed = (2.0 / (CHANNEL_SWITCH_DELAY_W + CELL_PROGRAM_DELAY + REG_WRITE_DELAY + CELL_PROGRAM_DELAY)) * SEC_IN_USEC,
                    .read_count = 2,
                    .read_speed = (2.0 / (REG_READ_DELAY + CELL_READ_DELAY + BLOCK_ERASE_DELAY + CHANNEL_SWITCH_DELAY_R + CELL_READ_DELAY)) * SEC_IN_USEC,
                    .garbage_collection_count = 1,
                    .write_amplification = 1.0,
                    .utilization = 1.0 / PAGES_IN_SSD
            },
            // garbage collection
            {
                    .write_count = 2,
                    .write_speed = (2.0 / (CHANNEL_SWITCH_DELAY_W + CELL_PROGRAM_DELAY + REG_WRITE_DELAY + CELL_PROGRAM_DELAY)) * SEC_IN_USEC,
                    .read_count = 2,
                    .read_speed = (2.0 / (REG_READ_DELAY + CELL_READ_DELAY + BLOCK_ERASE_DELAY + CHANNEL_SWITCH_DELAY_R + CELL_READ_DELAY)) * SEC_IN_USEC,
                    .garbage_collection_count = 2,
                    .write_amplification = 1.0,
                    .utilization = 1.0 / PAGES_IN_SSD
            }
    };

    void write() {
        struct timeval logging_parser_tv;
        Logger_Pool* logger = _analyzer->logger;

        TIME_MICROSEC(start);
        TIME_MICROSEC(end1);
        LOG_REGISTER_READ(logger, (RegisterReadLog) {
            .channel = 19, .die = 20, .reg = 21,
            .metadata = {start, end1}
        });
        TIME_MICROSEC(end2);
        LOG_PHYSICAL_CELL_READ(logger, (PhysicalCellReadLog) {
            .channel = 1, .block = 2, .page = 3,
            .metadata = {start, end2}
        });
        TIME_MICROSEC(end3);
        LOG_CHANNEL_SWITCH_TO_WRITE(logger, (ChannelSwitchToWriteLog) {
            .channel = 28,
            .metadata = {start, end3}
        });
        TIME_MICROSEC(end4);
        LOG_PHYSICAL_CELL_PROGRAM(logger, (PhysicalCellProgramLog) {
            .channel = 4, .flash=1, .block = 5, .page = 6,
            .metadata = {start, end4}
        });
        TIME_MICROSEC(end5);
        LOG_GARBAGE_COLLECTION(logger, (GarbageCollectionLog) empty_log);
        LOG_LOGICAL_CELL_PROGRAM(logger, (LogicalCellProgramLog) {
            .channel = 7, .block = 8, .page = 9,
            .metadata = {start, end5}
        });
        TIME_MICROSEC(end6);
        LOG_REGISTER_WRITE(logger, (RegisterWriteLog) {
            .channel = 22, .die = 23, .reg = 24,
            .metadata = {start, end6}
        });
        TIME_MICROSEC(end7);
        LOG_PHYSICAL_CELL_PROGRAM(logger, (PhysicalCellProgramLog) {
            .channel = 10, .flash=1, .block = 11, .page = 12,
            .metadata = {start, end7}
        });
        TIME_MICROSEC(end8);
        LOG_LOGICAL_CELL_PROGRAM(logger, (LogicalCellProgramLog) {
            .channel = 13, .block = 14, .page = 15,
            .metadata = {start, end8}
        });
        TIME_MICROSEC(end9);
        LOG_BLOCK_ERASE(logger, (BlockEraseLog) {
            .channel = 4, .die=1, .block = 5,
            .metadata = {start, end9}
        });
        TIME_MICROSEC(end10);
        LOG_CHANNEL_SWITCH_TO_READ(logger, (ChannelSwitchToReadLog) {
            .channel = 29,
            .metadata = {start, end10}
        });
        TIME_MICROSEC(end11);
        LOG_PHYSICAL_CELL_READ(logger, (PhysicalCellReadLog) {
            .channel = 16, .block = 17, .page = 18,
            .metadata = {start, end11}
        });
        LOG_GARBAGE_COLLECTION(logger, (GarbageCollectionLog) empty_log);
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
