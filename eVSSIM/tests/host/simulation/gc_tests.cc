/*
 * Copyright 2025 The Open University of Israel
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

extern LogServer log_server;

namespace ssd_io_emulator_tests {

    class GCUnitTest : public BaseTest {
        public:
            virtual void SetUp() {
                BaseTest::SetUp();
                INIT_LOG_MANAGER(g_device_index);

                pthread_mutex_lock(&g_lock); // prevent the GC thread from running
                devices[g_device_index].gc_low_thr_interval_sec = 0;
                devices[g_device_index].gc_hi_thr_interval_sec = 0;

                // Since there's a short duration in which g_lock is unlocked, the GC thread may
                // have done a loop already. We reset it to run ASAP.
                if (gc_threads[g_device_index].gc_loop_count > 0) {
                    pthread_cond_signal(&gc_threads[g_device_index].gc_signal_cond);
                    gc_threads[g_device_index].gc_loop_count = 0;
                }
            }

            virtual void TearDown() {
                pthread_mutex_unlock(&g_lock);
                BaseTest::TearDown(false);
                TERM_LOG_MANAGER(g_device_index);
                TERM_SSD_CONFIG();
            }
    };

    std::vector<SSDConf*> GetTestParams() {
        std::vector<SSDConf*> ssd_configs;

        ssd_configs.push_back(new SSDConf(parameters::Allsizemb[0]));

        return ssd_configs;
    }

    INSTANTIATE_TEST_CASE_P(DiskSize, GCUnitTest, ::testing::ValuesIn(GetTestParams()));

    static bool WaitForGC() {
        uint64_t before = gc_threads[g_device_index].gc_loop_count;
        for (int i = 0; i < 100; i++) {
            pthread_mutex_unlock(&g_lock);
            usleep(1000);
            pthread_mutex_lock(&g_lock);
            if (gc_threads[g_device_index].gc_loop_count > before) {
                return true;
            }
        }
        return false;
    }

    TEST_P(GCUnitTest, CaseBackgroundGC) {
        MONITOR_SYNC(g_device_index, &(log_server.stats), MONITOR_SLEEP_MAX_USEC);
        ASSERT_EQ(0, log_server.stats.garbage_collection_count);
        ASSERT_EQ(0, gc_threads[g_device_index].gc_loop_count);

        ASSERT_EQ(0, inverse_mappings_manager[g_device_index].total_victim_block_nb);
        ASSERT_EQ(devices[g_device_index].block_mapping_entry_nb, inverse_mappings_manager[g_device_index].total_empty_block_nb);
        while (inverse_mappings_manager[g_device_index].total_empty_block_nb > devices[g_device_index].block_mapping_entry_nb / 2) {
            uint64_t lpn = 0;
            uint64_t ppn;
            ASSERT_EQ(FTL_SUCCESS, GET_NEW_PAGE(g_device_index, VICTIM_OVERALL, devices[g_device_index].empty_table_entry_nb, &ppn));
            ASSERT_EQ(FTL_SUCCESS, UPDATE_NEW_PAGE_MAPPING(g_device_index, lpn, ppn));
            ASSERT_EQ(FTL_SUCCESS, UPDATE_OLD_PAGE_MAPPING(g_device_index, lpn));
        }
        ASSERT_EQ(devices[g_device_index].block_mapping_entry_nb / 2, inverse_mappings_manager[g_device_index].total_victim_block_nb);
        ASSERT_EQ(devices[g_device_index].block_mapping_entry_nb / 2, inverse_mappings_manager[g_device_index].total_empty_block_nb);

        MONITOR_SYNC(g_device_index, &(log_server.stats), MONITOR_SLEEP_MAX_USEC);
        ASSERT_EQ(0, log_server.stats.garbage_collection_count);
        ASSERT_EQ(0, gc_threads[g_device_index].gc_loop_count);

        ASSERT_TRUE(WaitForGC()) << "GC thread did not advance";

        MONITOR_SYNC(g_device_index, &(log_server.stats), MONITOR_SLEEP_MAX_USEC);
        ASSERT_LT(0, log_server.stats.garbage_collection_count);
        ASSERT_LT(0, gc_threads[g_device_index].gc_loop_count);

        ASSERT_GT(devices[g_device_index].block_mapping_entry_nb / 2, inverse_mappings_manager[g_device_index].total_victim_block_nb);
        ASSERT_LT(devices[g_device_index].block_mapping_entry_nb / 2, inverse_mappings_manager[g_device_index].total_empty_block_nb);
    }
}
