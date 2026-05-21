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

#include "base_emulator_tests.h"
#include "vssim_config_manager.h"

using namespace std;

namespace ssd_conf_tests {

    class SSDConfigTest : public BaseTest {
        public:
            virtual void SetUp() {
                BaseTest::SetUp();
                INIT_LOG_MANAGER(g_device_index);
                pthread_mutex_lock(&g_lock); // prevent the GC thread from running
            }

            virtual void TearDown() {
                pthread_mutex_unlock(&g_lock);
                BaseTest::TearDown(false);
                TERM_LOG_MANAGER(g_device_index);
                TERM_SSD_CONFIG();
            }
    };

    // Test parameter generation
    std::vector<SSDConf*> GetTestParams() {
        std::vector<SSDConf*> ssd_configs;
        // Add configurations with different parameters
        int onfi_manager_threads = 7;
        ssd_configs.push_back(new SSDConf(parameters::Allsizemb[0], onfi_manager_threads));
        return ssd_configs;
    }

    INSTANTIATE_TEST_CASE_P(DiskSize, SSDConfigTest, ::testing::ValuesIn(GetTestParams()));

    TEST_P(SSDConfigTest, ParseBasicConfigValues) {
        SSDConf* ssd_config = base_test_get_ssd_config();

        // Verify that parsed values match the serialized config

        // Verify all the config params
        ASSERT_EQ(ssd_config->get_page_size(), devices[g_device_index].page_size);
        ASSERT_EQ(ssd_config->get_page_nb(), devices[g_device_index].page_nb);
        ASSERT_EQ(ssd_config->get_sector_size(), devices[g_device_index].sector_size);
        ASSERT_EQ(ssd_config->get_flash_nb(), devices[g_device_index].flash_nb);
        ASSERT_EQ(ssd_config->get_block_nb(), devices[g_device_index].block_nb);
        ASSERT_EQ(1, devices[g_device_index].planes_per_flash);
        ASSERT_EQ(82, devices[g_device_index].reg_write_delay);
        ASSERT_EQ(900, devices[g_device_index].cell_program_delay);
        ASSERT_EQ(82, devices[g_device_index].reg_read_delay);
        ASSERT_EQ(50, devices[g_device_index].cell_read_delay);
        ASSERT_EQ(2000, devices[g_device_index].block_erase_delay);
        ASSERT_EQ(16, devices[g_device_index].channel_switch_delay_r);
        ASSERT_EQ(33, devices[g_device_index].channel_switch_delay_w);
        ASSERT_EQ(ssd_config->get_channel_nb(), devices[g_device_index].channel_nb);
        ASSERT_EQ(15, devices[g_device_index].stat_type);
        ASSERT_EQ(62, devices[g_device_index].stat_scope);
        ASSERT_EQ(ssd_config->get_storage_strategy(), devices[g_device_index].storage_strategy);
        ASSERT_EQ(20, devices[g_device_index].gc_low_thr);
        ASSERT_EQ(80, devices[g_device_index].gc_hi_thr);
        ASSERT_EQ(ssd_config->get_onfi_manager_threads(), devices[g_device_index].onfi_manager_threads);

        // FILENAME depends on device index
        std::string expected_file_name = "./data/ssd" +
            (g_device_index == 0 ? "" : std::to_string(g_device_index + 1)) + ".img";
        ASSERT_EQ(expected_file_name, std::string(devices[g_device_index].file_name));


        // Namespace sizes (calculated in serialization)
        ASSERT_EQ((ssd_config->get_block_nb() / 2), devices[g_device_index].namespaces_size[0]); // NS1
        ASSERT_EQ((ssd_config->get_block_nb() / 4), devices[g_device_index].namespaces_size[1]); // NS2

        // STAT_PATH depends on device index
        std::string expected_stat_path = " /tmp/stat" + (g_device_index == 0 ? "" : std::to_string(g_device_index + 1)) + ".csv\n";
        ASSERT_EQ(expected_stat_path, std::string(devices[g_device_index].stat_path));

    }

    TEST_P(SSDConfigTest, VerifyDerivedValues) {
        // Verify derived values calculated by calculate_derived_values()
        EXPECT_EQ(devices[g_device_index].sectors_per_page, devices[g_device_index].page_size / devices[g_device_index].sector_size);
        EXPECT_EQ(devices[g_device_index].pages_per_flash, devices[g_device_index].page_nb * devices[g_device_index].block_nb);
        EXPECT_EQ(devices[g_device_index].block_mapping_entry_nb, devices[g_device_index].block_nb* devices[g_device_index].flash_nb);
        EXPECT_EQ(devices[g_device_index].pages_in_ssd, devices[g_device_index].page_nb * devices[g_device_index].block_nb * devices[g_device_index].flash_nb);
        EXPECT_EQ(devices[g_device_index].sectors_in_ssd, devices[g_device_index].sectors_per_page * (devices[g_device_index].pages_in_ssd - devices[g_device_index].page_nb));

    #ifdef PAGE_MAP
        EXPECT_EQ(devices[g_device_index].page_mapping_entry_nb, devices[g_device_index].page_nb * devices[g_device_index].block_nb * devices[g_device_index].flash_nb);
        EXPECT_EQ(devices[g_device_index].each_empty_table_entry_nb, devices[g_device_index].block_nb / devices[g_device_index].planes_per_flash);
        EXPECT_EQ(devices[g_device_index].empty_table_entry_nb, devices[g_device_index].flash_nb * devices[g_device_index].planes_per_flash);
        EXPECT_EQ(devices[g_device_index].victim_table_entry_nb, devices[g_device_index].flash_nb * devices[g_device_index].planes_per_flash);
        EXPECT_EQ(devices[g_device_index].data_block_nb, devices[g_device_index].block_nb);

        EXPECT_EQ(0.2, devices[g_device_index].gc_threshold);
            EXPECT_EQ(devices[g_device_index].gc_threshold_block_nb, (int)((1-devices[g_device_index].gc_threshold) * (double)devices[g_device_index].block_mapping_entry_nb));
        EXPECT_EQ(devices[g_device_index].gc_threshold_block_nb_each, (int)((1-devices[g_device_index].gc_threshold) * (double)devices[g_device_index].each_empty_table_entry_nb));
        EXPECT_EQ(devices[g_device_index].gc_victim_nb, (int)(devices[g_device_index].gc_threshold_block_nb * 0.5));
        EXPECT_EQ(devices[g_device_index].gc_l2_threshold_block_nb, (int)(0.9 * (double)devices[g_device_index].block_mapping_entry_nb));
    #endif // PAGE_MAP

        EXPECT_EQ(devices[g_device_index].gc_low_thr_page_nb, devices[g_device_index].page_nb * (100 - devices[g_device_index].gc_low_thr) * devices[g_device_index].block_mapping_entry_nb / 100);
        EXPECT_EQ(devices[g_device_index].gc_hi_thr_page_nb, devices[g_device_index].page_nb * (100 - devices[g_device_index].gc_hi_thr) * devices[g_device_index].block_mapping_entry_nb / 100);
        EXPECT_EQ(10, devices[g_device_index].gc_low_thr_interval_sec);
        EXPECT_EQ(1, devices[g_device_index].gc_hi_thr_interval_sec);
    }

} //namespace
