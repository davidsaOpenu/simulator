/*
 * Copyright 2026 The Open University of Israel
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
#include <string>

using namespace std;

namespace event_generator_tests {

const char* GENERATOR = "/code/simulator/infra/ELK/event_generator.py";
const char* STREAM = "/tmp/event_stream.txt";
const int DISKS = 3;

/** Replay the generator's stream through the real log manager: one logger and offline analyzer per disk.
 * The 3 disks are processed sequentially: disk 0 drains fully, then disk 1, then disk 2. */
static void replay() {
    Logger_Pool* loggers[DISKS];
    for (int d = 0; d < DISKS; d++)
        loggers[d] = logger_init(DEFUALT_LOGGER_POOL_SIZE);

    FILE* stream = fopen(STREAM, "r");
    ASSERT_TRUE(stream != NULL);
    char type[64];
    unsigned int disk, channel, die, block, page;
    long long start, end;
    while (fscanf(stream, "%u %63s %u %u %u %u %lld %lld", &disk, type, &channel, &die, &block, &page, &start, &end) == 8) {
        ASSERT_LT(disk, (unsigned int) DISKS);
        LogMetadata meta;
        memset(&meta, 0, sizeof(meta));
        meta.logging_start_time = start;
        meta.logging_end_time = end;
        if (strcmp(type, "PhysicalCellProgramLog") == 0) {
            PhysicalCellProgramLog log = { .channel = channel, .block = block, .page = page, .background = false, .metadata = meta };
            LOG_PHYSICAL_CELL_PROGRAM(loggers[disk], log);
        } else if (strcmp(type, "LogicalCellProgramLog") == 0) {
            LogicalCellProgramLog log = { .channel = channel, .block = block, .page = page, .metadata = meta };
            LOG_LOGICAL_CELL_PROGRAM(loggers[disk], log);
        } else if (strcmp(type, "PhysicalCellReadLog") == 0) {
            PhysicalCellReadLog log = { .channel = channel, .block = block, .page = page, .background = false, .metadata = meta };
            LOG_PHYSICAL_CELL_READ(loggers[disk], log);
        } else if (strcmp(type, "BlockEraseLog") == 0) {
            BlockEraseLog log = { .channel = channel, .die = die, .block = block, .dirty_page_nb = 0, .background = false, .metadata = meta };
            LOG_BLOCK_ERASE(loggers[disk], log);
        } else {
            ADD_FAILURE() << "unknown event type " << type;
        }
    }
    fclose(stream);

    elk_logger_writer_init();
    for (int d = 0; d < DISKS; d++) {
        OfflineLogAnalyzer* analyzer = offline_log_analyzer_init(loggers[d], d);
        // exit flag raised before the loop runs: it drains the whole queue once and returns
        analyzer->exit_loop_flag = 1;
        offline_log_analyzer_loop(analyzer);
        offline_log_analyzer_free(analyzer);
        logger_free(loggers[d]);
    }
    elk_logger_writer_free();
}

// Excluded from run_all_host_tests.sh: runs only through the ELK gate, in the container where GENERATOR exists.
// Produces the logs only. host_metrics.py gate checks them in Elasticsearch against the tally.
TEST(EventGeneratorTest, Replay) {
    ASSERT_EQ(0, system((string("python3 ") + GENERATOR + " " + STREAM).c_str()));
    replay();
}
} //namespace
