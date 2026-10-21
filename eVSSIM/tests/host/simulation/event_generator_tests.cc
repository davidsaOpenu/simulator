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

#include "base_emulator_tests.h"
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using namespace std;

namespace event_generator_tests {

const char* STREAM = "/tmp/event_stream.txt";
const int DISKS = 3;
const size_t DISK_SIZE_MB = 16;

static bool arrive_all(atomic<int>& arrived, int parties) {
    arrived++;
    auto deadline = chrono::steady_clock::now() + chrono::seconds(30);
    while (arrived < parties) {
        if (chrono::steady_clock::now() > deadline)
            return false;
        this_thread::sleep_for(chrono::milliseconds(1));
    }
    return true;
}

/** Replay the generator's stream through the real log manager: one logger and offline analyzer per disk, one thread per disk. */
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
    atomic<int> arrived(0);
    vector<char> ok(DISKS, 0);
    vector<thread> workers;
    for (int d = 0; d < DISKS; d++) {
        workers.emplace_back([&, d]() {
            if (!arrive_all(arrived, DISKS))
                return;
            OfflineLogAnalyzer* analyzer = offline_log_analyzer_init(loggers[d], d);
            // exit flag raised before the loop runs: it drains the whole queue once and returns
            analyzer->exit_loop_flag = 1;
            offline_log_analyzer_loop(analyzer);
            offline_log_analyzer_free(analyzer);
            logger_free(loggers[d]);
            ok[d] = 1;
        });
    }
    for (auto& worker : workers)
        worker.join();
    for (int d = 0; d < DISKS; d++)
        EXPECT_TRUE(ok[d]) << "disk " << d << " did not finish replay";
    elk_logger_writer_free();
}

// Excluded from run_all_host_tests.sh: runs only through the ELK gate, in the container where the generator exists.
// Produces the logs only. host_metrics.py gate checks them in Elasticsearch against the tally.
TEST(EventGeneratorTest, Replay) {
    const char* root = getenv("EVSSIM_DOCKER_ROOT_PATH");
    const char* simulator = getenv("EVSSIM_SIMULATOR_FOLDER");
    const char* tally_path = getenv("EVSSIM_GENERATOR_TALLY");
    ASSERT_TRUE(root != NULL && simulator != NULL && tally_path != NULL);
    string generator = string(root) + "/" + simulator + "/infra/ELK/event_generator.py";
    ASSERT_EQ(0, system(("python3 " + generator + " " + STREAM + " " + tally_path).c_str()));
    replay();
}

// Every victim block is dead, so each collection copies 0 pages and logs exactly 1 GarbageCollectionLog,
// 1 BlockEraseLog and 1 SsdUtilizationLog (SSD_BLOCK_ERASE logs it).
TEST(EventGeneratorTest, GarbageCollection) {
    ASSERT_TRUE(g_no_bg_gc) << "run with --no-bg-gc";
    const char* tally_path = getenv("EVSSIM_GENERATOR_TALLY");
    ASSERT_TRUE(tally_path != NULL);
    SSDConf(DISK_SIZE_MB).ssd_conf_serialize();
    INIT_SSD_CONFIG();

    for (uint8_t d = 0; d < device_count; d++) {
        FTL_INIT(d);
        INIT_LOG_MANAGER(d);
        ASSERT_LT(0, devices[d].gc_victim_nb);
    }

    atomic<int> arrived(0);
    vector<uint64_t> collections(device_count, 0);
    vector<char> ok(device_count, 0);
    vector<thread> workers;
    for (uint8_t d = 0; d < device_count; d++) {
        workers.emplace_back([&, d]() {
            if (!arrive_all(arrived, device_count))
                return;
            [&]() {
                ssd_config_t* config = &devices[d];
                uint64_t* zero_pages = &inverse_mappings_manager[d].total_zero_page_nb;
                uint64_t pages_total = config->pages_in_ssd - config->page_nb;
                for (uint64_t lpn = 0; lpn < pages_total; lpn++) {
                    uint64_t ppn;
                    ASSERT_EQ(FTL_SUCCESS, GET_NEW_PAGE(d, VICTIM_OVERALL, config->empty_table_entry_nb, &ppn));
                    ASSERT_EQ(FTL_SUCCESS, UPDATE_NEW_PAGE_MAPPING(d, lpn, ppn));
                }
                for (uint64_t lpn = 0; lpn < pages_total; lpn++)
                    ASSERT_EQ(FTL_SUCCESS, UPDATE_OLD_PAGE_MAPPING(d, lpn));

                uint64_t threshold = (uint64_t) config->gc_threshold_block_nb * config->page_nb;
                while (*zero_pages < threshold) {
                    uint64_t before = *zero_pages;
                    ASSERT_TRUE(GC_CHECK(d, false, false));
                    ASSERT_EQ(before + (uint64_t) config->gc_victim_nb * config->page_nb, *zero_pages);
                    collections[d] += config->gc_victim_nb;
                }
                ASSERT_FALSE(GC_CHECK(d, false, false));
                ok[d] = 1;
            }();
        });
    }
    for (auto& worker : workers)
        worker.join();

    ofstream tally(tally_path);
    tally << "{";
    for (uint8_t d = 0; d < device_count; d++) {
        EXPECT_TRUE(ok[d]) << "disk " << (int) d << " did not finish";
        EXPECT_LT(0u, collections[d]) << "disk " << (int) d;
        FTL_TERM(d);
        TERM_LOG_MANAGER(d);
        tally << (d ? ", " : "") << "\"" << (int) d << "\": {\"GarbageCollectionLog\": " << collections[d]
              << ", \"BlockEraseLog\": " << collections[d] << ", \"SsdUtilizationLog\": " << collections[d] << "}";
    }
    tally << "}\n";
    TERM_SSD_CONFIG();
}
} //namespace
