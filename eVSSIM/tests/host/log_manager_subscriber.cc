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
#include "logging_backend.h"
#include "logging_rt_analyzer.h"
#include "logging_manager.h"
}

#include <gtest/gtest.h>

#include <fstream>
#include <cstdio>
#include <cstdlib>


using namespace std;

namespace manager_subscriber {
    int updates_done;
    /**
     * The first logger to use
     */
    Logger* logger1;
    /**
     * The second logger to use
     */
    Logger* logger2;
    /**
     * The analyzer of the first logger
     */
    RTLogAnalyzer* analyzer1;
    /**
     * The analyzer of the second logger
     */
    RTLogAnalyzer* analyzer2;
    /**
     * The log manager to use
     */
    LogManager* manager;

    /**
     * Assert that the statistics given are OK
     * @param stats the statistics to validate
     */
    void on_update(SSDStatistics stats, void*) {
        updates_done++;

        // in the first update check for empty stats
        if (updates_done == 1) {
            ASSERT_EQ(0, stats.write_count);
            ASSERT_EQ(0.0, stats.write_speed);
            ASSERT_EQ(0, stats.read_count);
            ASSERT_EQ(0.0, stats.read_speed);
            ASSERT_EQ(0, stats.garbage_collection_count);
            ASSERT_EQ(0.0, stats.write_amplification);
            ASSERT_EQ(0.0, stats.utilization);
        }
        // in the second update check for the right values
        else if (updates_done == 2) {
            ASSERT_EQ(3, stats.write_count);
            ASSERT_EQ((1.0 / (900)) * 1000000.0, stats.write_speed);
            ASSERT_EQ(1, stats.read_count);
            ASSERT_EQ((1.0 / (50)) * 1000000.0, stats.read_speed);
            ASSERT_EQ(1, stats.garbage_collection_count);
            ASSERT_EQ(1.5, stats.write_amplification);
            ASSERT_EQ(3.0 / (2.0 * 4.0 * 8.0), stats.utilization);
        }
        // the test isn't supposed to check other cases
        else {
            FAIL() << "the test is not configured to check more updates";
        }
    }

    void init(LogManager* manager) {
        updates_done = 0;
        logger1 = logger_init(128);
        logger2 = logger_init(128);
        analyzer1 = rt_log_analyzer_init(logger1);
        analyzer2 = rt_log_analyzer_init(logger2);
        log_manager_add_analyzer(manager, analyzer1);
        log_manager_add_analyzer(manager, analyzer2);
        log_manager_subscribe(manager, (MonitorHook) on_update, NULL);
        manager_subscriber::manager = manager;
    }

    void run() {
        log_manager_loop(manager, 1);

        // write the different logs to the different loggers
        GarbageCollectionLog GC_LOG;
        LOG_PHYSICAL_CELL_PROGRAM(logger1, (PhysicalCellProgramLog) { .channel = 1, .block = 2, .page = 3});
        LOG_PHYSICAL_CELL_PROGRAM(logger1, (PhysicalCellProgramLog) { .channel = 4, .block = 5, .page = 6});
        LOG_LOGICAL_CELL_PROGRAM(logger1, (LogicalCellProgramLog) { .channel = 7, .block = 8, .page = 9});
        LOG_PHYSICAL_CELL_PROGRAM(logger2, (PhysicalCellProgramLog) { .channel = 10, .block = 11, .page = 12});
        LOG_LOGICAL_CELL_PROGRAM(logger2, (LogicalCellProgramLog) { .channel = 13, .block = 14, .page = 15});
        LOG_PHYSICAL_CELL_READ(logger2, (PhysicalCellReadLog) { .channel = 16, .block = 17, .page = 18 });
        LOG_GARBAGE_COLLECTION(logger1, GC_LOG);

        rt_log_analyzer_loop(analyzer1, 4);
        rt_log_analyzer_loop(analyzer2, 3);

        log_manager_loop(manager, 1);
    }

    void free() {
        rt_log_analyzer_free(analyzer1, 1);
        rt_log_analyzer_free(analyzer2, 1);
    }
}
