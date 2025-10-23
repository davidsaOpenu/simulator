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

        ssd_configs.push_back(new SSDConf(parameters::sizemb::mb1));

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
        ssd_config_t *config = &devices[g_device_index];

        ASSERT_EQ(0, gc_threads[g_device_index].gc_loop_count);
        ASSERT_EQ(config->pages_in_ssd, inverse_mappings_manager[g_device_index].total_zero_page_nb);

        for (uint64_t i = 0; i < config->pages_in_ssd / 2; i++) {
            uint64_t lpn = 0;
            uint64_t ppn;
            ASSERT_EQ(FTL_SUCCESS, GET_NEW_PAGE(g_device_index, VICTIM_OVERALL, config->empty_table_entry_nb, &ppn));
            ASSERT_EQ(FTL_SUCCESS, UPDATE_NEW_PAGE_MAPPING(g_device_index, lpn, ppn));
            ASSERT_EQ(FTL_SUCCESS, UPDATE_OLD_PAGE_MAPPING(g_device_index, lpn));
        }

        ASSERT_EQ(0, gc_threads[g_device_index].gc_loop_count);
        ASSERT_EQ(config->pages_in_ssd / 2, inverse_mappings_manager[g_device_index].total_zero_page_nb);

        ASSERT_TRUE(WaitForGC()) << "GC thread did not advance";

        ASSERT_LT(0, gc_threads[g_device_index].gc_loop_count);
        ASSERT_LT(config->pages_in_ssd / 2, inverse_mappings_manager[g_device_index].total_zero_page_nb);
    }

    TEST_P(GCUnitTest, CaseLowThresholdInterval) {
        ssd_config_t *config = &devices[g_device_index];

        config->gc_hi_thr_interval_sec = 100 * 3600; // 100 hours - practically infinity

        ASSERT_TRUE(WaitForGC()) << "GC thread did not advance";
        ASSERT_TRUE(WaitForGC()) << "GC thread did not advance";
    }

    TEST_P(GCUnitTest, CaseHighThresholdInterval) {
        ssd_config_t *config = &devices[g_device_index];

        for (uint64_t i = 0; i < config->pages_in_ssd / 2; i++) {
            uint64_t lpn = i;
            uint64_t ppn;
            ASSERT_EQ(FTL_SUCCESS, GET_NEW_PAGE(g_device_index, VICTIM_OVERALL, config->empty_table_entry_nb, &ppn));
            ASSERT_EQ(FTL_SUCCESS, UPDATE_NEW_PAGE_MAPPING(g_device_index, lpn, ppn));
        }

        config->gc_low_thr_interval_sec = 100 * 3600; // 100 hours - practically infinity

        ASSERT_TRUE(WaitForGC()) << "GC thread did not advance";
        ASSERT_TRUE(WaitForGC()) << "GC thread did not advance";
    }

    TEST_P(GCUnitTest, CaseWaitForInvalidPage) {
        ssd_config_t *config = &devices[g_device_index];

        ASSERT_EQ(0, gc_threads[g_device_index].gc_loop_count);
        ASSERT_EQ(config->pages_in_ssd, inverse_mappings_manager[g_device_index].total_zero_page_nb);

        // total usable pages (minus reserved zero pages for GC)
        uint64_t pages_total = config->pages_in_ssd - config->page_nb;

        uint64_t ppn;
        for (uint64_t i = 0; i < pages_total; i++) {
            uint64_t lpn = i;
            ASSERT_EQ(FTL_SUCCESS, GET_NEW_PAGE(g_device_index, VICTIM_OVERALL, devices[g_device_index].empty_table_entry_nb, &ppn));
            ASSERT_EQ(FTL_SUCCESS, UPDATE_NEW_PAGE_MAPPING(g_device_index, lpn, ppn));
        }
        ASSERT_EQ(FTL_FAILURE, GET_NEW_PAGE(g_device_index, VICTIM_OVERALL, devices[g_device_index].empty_table_entry_nb, &ppn));

        ASSERT_EQ(0, gc_threads[g_device_index].gc_loop_count);
        ASSERT_EQ(config->page_nb, inverse_mappings_manager[g_device_index].total_zero_page_nb);

        ASSERT_TRUE(WaitForGC()) << "GC thread did not advance";

        // thread should be waiting for new invalid pages right now.
        ASSERT_EQ(1, gc_threads[g_device_index].gc_loop_count);
        ASSERT_EQ(config->page_nb, inverse_mappings_manager[g_device_index].total_zero_page_nb);

        ASSERT_FALSE(WaitForGC()) << "GC thread should not have advanced";

        ASSERT_EQ(1, gc_threads[g_device_index].gc_loop_count);
        ASSERT_EQ(config->page_nb, inverse_mappings_manager[g_device_index].total_zero_page_nb);

        for (uint64_t i = 0; i < pages_total; i++) {
            uint64_t lpn = i;
            ASSERT_EQ(FTL_SUCCESS, UPDATE_OLD_PAGE_MAPPING(g_device_index, lpn));
        }

        ASSERT_EQ(1, gc_threads[g_device_index].gc_loop_count);
        ASSERT_EQ(config->page_nb, inverse_mappings_manager[g_device_index].total_zero_page_nb);

        ASSERT_TRUE(WaitForGC()) << "GC thread did not advance";

        ASSERT_LT(1, gc_threads[g_device_index].gc_loop_count);
        ASSERT_LT(config->page_nb, inverse_mappings_manager[g_device_index].total_zero_page_nb);
    }

    TEST_P(GCUnitTest, CaseDiskFullWrite) {
        ssd_config_t *config = &devices[g_device_index];

        // total usable pages (minus reserved zero pages for GC)
        uint64_t pages_total = config->pages_in_ssd - config->page_nb;

        uint64_t ppn;
        for (uint64_t i = 0; i < pages_total; i++) {
            uint64_t lpn = i;
            ASSERT_EQ(FTL_SUCCESS, GET_NEW_PAGE(g_device_index, VICTIM_OVERALL, devices[g_device_index].empty_table_entry_nb, &ppn));
            ASSERT_EQ(FTL_SUCCESS, UPDATE_NEW_PAGE_MAPPING(g_device_index, lpn, ppn));
        }
        ASSERT_EQ(FTL_FAILURE, GET_NEW_PAGE(g_device_index, VICTIM_OVERALL, devices[g_device_index].empty_table_entry_nb, &ppn));

        ASSERT_EQ(0, gc_threads[g_device_index].gc_loop_count);
        ASSERT_TRUE(WaitForGC()) << "GC thread did not advance";
        // thread should be waiting for new invalid pages right now.
        ASSERT_FALSE(WaitForGC()) << "GC thread should not have advanced";
        ASSERT_EQ(1, gc_threads[g_device_index].gc_loop_count);

        // several arbitrary sectors:
        uint64_t sectors[] = {
            0, 1,
            config->sectors_in_ssd / 2 - 1, config->sectors_in_ssd / 2, config->sectors_in_ssd / 2 + 1,
            config->sectors_in_ssd - 2, config->sectors_in_ssd - 1};

        // "sparse surface scan" when disk is completely full
        for (size_t i = 0; i < sizeof(sectors) / sizeof(sectors[0]); i++) {
            uint64_t lba = sectors[i];
            ASSERT_EQ(config->page_nb, inverse_mappings_manager[g_device_index].total_zero_page_nb);
            pthread_mutex_unlock(&g_lock);
            ASSERT_EQ(FTL_SUCCESS, FTL_WRITE_SECT(g_device_index, lba, 1, NULL));
            pthread_mutex_lock(&g_lock);
            ASSERT_EQ(config->page_nb, inverse_mappings_manager[g_device_index].total_zero_page_nb);
        }
    }

    TEST_P(GCUnitTest, CaseReservedLBAs) {
        ssd_config_t *config = &devices[g_device_index];

        pthread_mutex_unlock(&g_lock);
        // "Exceed Sector number" error - reserved pages for GC
        ASSERT_EQ(FTL_FAILURE, FTL_WRITE_SECT(g_device_index, config->sectors_in_ssd, 1, NULL));
        ASSERT_EQ(FTL_SUCCESS, FTL_WRITE_SECT(g_device_index, config->sectors_in_ssd - 1, 1, NULL));
        pthread_mutex_lock(&g_lock);
    }
}
