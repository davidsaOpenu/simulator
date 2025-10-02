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

namespace ssd_io_emulator_tests {

    class GCUnitTest : public BaseTest {
        public:
            virtual void SetUp() {
                BaseTest::SetUp();
                pthread_mutex_lock(&g_lock);
                INIT_LOG_MANAGER(g_device_index);
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


    TEST_P(GCUnitTest, CaseBackgroundGC) {
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

        struct timespec ts = { .tv_sec = 0, .tv_nsec = 100 * 1000000L }; // 100 ms

        for (int i = 0; i < 20; i++) {
            pthread_mutex_unlock(&g_lock);
            nanosleep(&ts, NULL);
            pthread_mutex_lock(&g_lock);

            if (inverse_mappings_manager[g_device_index].total_empty_block_nb > devices[g_device_index].block_mapping_entry_nb / 2) {
                break;
            }
        }

        ASSERT_GT(devices[g_device_index].block_mapping_entry_nb / 2, inverse_mappings_manager[g_device_index].total_victim_block_nb);
        ASSERT_LT(devices[g_device_index].block_mapping_entry_nb / 2, inverse_mappings_manager[g_device_index].total_empty_block_nb);
    }
}
