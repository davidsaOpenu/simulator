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

/*
 * Tests for ssd.conf.template parsing and namespace capacity validation.
 *
 * The template uses %(EVSSIM_RUNTIME_STORAGE_STRATEGY)s placeholders; tests
 * substitute a concrete integer value before writing data/ssd.conf.
 */

#include "base_emulator_tests.h"
#include <gtest/gtest.h>
#include <fstream>
#include <string>
#include <sstream>
#include <sys/stat.h>
#include <cstring>
#include <cstdio>
#include <inttypes.h>

extern "C" {
#include "common.h"
#include "vssim_config_manager.h"
}

/* Disk geometry used in every template device. */
static const uint64_t TPL_PAGE_SIZE  = 4096;
static const uint64_t TPL_PAGE_NB    = 10;
static const uint64_t TPL_BLOCK_NB   = 4096;
static const uint32_t TPL_FLASH_NB   = 4;
static const uint64_t TPL_NS_SIZE    = 16736256; /* bytes per namespace */

static uint64_t disk_bytes_from_template(void) {
    return TPL_PAGE_SIZE * TPL_PAGE_NB * TPL_BLOCK_NB * TPL_FLASH_NB;
}

/* -------------------------------------------------------------------------
 * Test fixture
 *
 * Note: SsdConfTest cannot inherit BaseTest because BaseTest is parameterised
 * on SSDConf* and drives INIT_SSD_CONFIG internally.  These tests need to
 * control the raw file content for each case, so they manage the lifecycle
 * directly — following the same mkdir / TERM_SSD_CONFIG / remove conventions.
 * ------------------------------------------------------------------------- */
class SsdConfTest : public ::testing::Test {
protected:
    void SetUp() override {
        mkdir("data", 0777);
    }

    void TearDown() override {
        TERM_SSD_CONFIG();
        remove("data/ssd.conf");
        // Remove per-device subdirectories created by INIT_SSD_CONFIG
        for (int d = 0; d < 9; d++) {
            char dir[16];
            snprintf(dir, sizeof(dir), "data/%d", d);
            rmdir(dir);
        }
    }

    /* Write content to data/ssd.conf (replaces the strategy placeholder). */
    void WriteConf(const std::string& content, int strategy = STRATEGY_SECTOR) {
        std::string resolved = content;
        const std::string placeholder = "%(EVSSIM_RUNTIME_STORAGE_STRATEGY)s";
        std::string strategy_str = std::to_string(strategy);
        size_t pos;
        while ((pos = resolved.find(placeholder)) != std::string::npos)
            resolved.replace(pos, placeholder.size(), strategy_str);

        std::ofstream f("data/ssd.conf", std::ios::trunc);
        f << resolved;
    }

    /* Sum of all namespace sizes for a parsed device. */
    static uint64_t ns_total(const ssd_config_t* dev) {
        uint64_t total = 0;
        for (int i = 0; i < MAX_NUMBER_OF_NAMESPACES; i++)
            total += dev->namespaces_size[i];
        return total;
    }
};

/* -------------------------------------------------------------------------
 * Template device building block
 * ------------------------------------------------------------------------- */
static std::string device_header(int num, const char* stat_suffix = "") {
    std::ostringstream s;
    s << "[nvme0" << num << "]\n"
      << "FILE_NAME ./data/ssd" << (num > 1 ? std::to_string(num) : "") << ".img\n"
      << "PAGE_SIZE 4096\n"
      << "PAGE_NB 10\n"
      << "SECTOR_SIZE 1\n"
      << "FLASH_NB 4\n"
      << "BLOCK_NB 4096\n"
      << "PLANES_PER_FLASH 1\n"
      << "REG_WRITE_DELAY 82\n"
      << "CELL_PROGRAM_DELAY 900\n"
      << "REG_READ_DELAY 82\n"
      << "CELL_READ_DELAY 50\n"
      << "BLOCK_ERASE_DELAY 2000\n"
      << "CHANNEL_SWITCH_DELAY_R 16\n"
      << "CHANNEL_SWITCH_DELAY_W 33\n"
      << "CHANNEL_NB 4\n"
      << "STAT_TYPE 15\n"
      << "STAT_SCOPE 62\n"
      << "STAT_PATH /tmp/stat" << stat_suffix << ".csv\n"
      << "GC_LOW_THR 20\n"
      << "GC_HI_THR 80\n";
    return s.str();
}

static std::string ns_section(int ns_num, uint64_t size,
                               bool with_object_params = false) {
    std::ostringstream s;
    s << "[ns0" << ns_num << "]\n"
      << "STORAGE_STRATEGY %(EVSSIM_RUNTIME_STORAGE_STRATEGY)s\n"
      << "NAMESPACE_PAGE_NB 4096\n"
      << "SIZE " << size << "\n";
    if (with_object_params)
        s << "OBJECT_KEY_SIZE 16\n"
          << "OBJECT_MAX_VALUE_SIZE 4096\n"
          << "OBJECT_MAX_CAPACITY 4096\n";
    return s.str();
}

/* =========================================================================
 * 1. Parsing tests
 * ========================================================================= */

TEST_F(SsdConfTest, ParseSingleDeviceNoNamespace) {
    WriteConf(device_header(1));
    INIT_SSD_CONFIG();
    ASSERT_EQ(1, device_count);
    EXPECT_STREQ("nvme01", devices[0].device_name);
    EXPECT_EQ(4096u, devices[0].page_size);
    EXPECT_EQ(10u,   devices[0].page_nb);
    EXPECT_EQ(1u,    devices[0].sector_size);
    EXPECT_EQ(4u,    devices[0].flash_nb);
    EXPECT_EQ(4096u, devices[0].block_nb);
    EXPECT_EQ(1u,    devices[0].planes_per_flash);
    EXPECT_EQ(4u,    devices[0].channel_nb);
    EXPECT_EQ(82,    devices[0].reg_write_delay);
    EXPECT_EQ(900,   devices[0].cell_program_delay);
    EXPECT_EQ(82,    devices[0].reg_read_delay);
    EXPECT_EQ(50,    devices[0].cell_read_delay);
    EXPECT_EQ(2000,  devices[0].block_erase_delay);
    EXPECT_EQ(16,    devices[0].channel_switch_delay_r);
    EXPECT_EQ(33,    devices[0].channel_switch_delay_w);
    EXPECT_EQ(15,    devices[0].stat_type);
    EXPECT_EQ(62,    devices[0].stat_scope);
    EXPECT_EQ(20,    devices[0].gc_low_thr);
    EXPECT_EQ(80,    devices[0].gc_hi_thr);
}

TEST_F(SsdConfTest, ParseSingleDeviceSingleNamespace) {
    WriteConf(device_header(1) + ns_section(1, TPL_NS_SIZE));
    INIT_SSD_CONFIG();
    ASSERT_EQ(1, device_count);
    EXPECT_EQ(TPL_NS_SIZE, devices[0].namespaces_size[0]);
    EXPECT_EQ(4096u, devices[0].ns_namespace_page_nb[0]);
    EXPECT_EQ(STRATEGY_SECTOR, devices[0].ns_storage_strategy[0]);
}

TEST_F(SsdConfTest, ParseSingleDeviceObjectNamespace) {
    WriteConf(device_header(1) + ns_section(1, TPL_NS_SIZE, true),
              STRATEGY_OBJECT);
    INIT_SSD_CONFIG();
    ASSERT_EQ(1, device_count);
    EXPECT_EQ(TPL_NS_SIZE, devices[0].namespaces_size[0]);
    EXPECT_EQ(STRATEGY_OBJECT, devices[0].ns_storage_strategy[0]);
    EXPECT_EQ(16u,   devices[0].ns_object_key_size[0]);
    EXPECT_EQ(4096u, devices[0].ns_object_max_value_size[0]);
    EXPECT_EQ(4096u, devices[0].ns_object_max_capacity[0]);
}

TEST_F(SsdConfTest, ParseTwoDevices) {
    WriteConf(device_header(1, "") + ns_section(1, TPL_NS_SIZE) +
              device_header(2, "2") + ns_section(1, TPL_NS_SIZE));
    INIT_SSD_CONFIG();
    ASSERT_EQ(2, device_count);
    EXPECT_STREQ("nvme01", devices[0].device_name);
    EXPECT_STREQ("nvme02", devices[1].device_name);
    EXPECT_EQ(TPL_NS_SIZE, devices[0].namespaces_size[0]);
    EXPECT_EQ(TPL_NS_SIZE, devices[1].namespaces_size[0]);
}

TEST_F(SsdConfTest, ParseMultipleNamespacesPerDevice) {
    /* nvme03-like: two namespaces */
    std::string conf = device_header(1)
                     + ns_section(1, TPL_NS_SIZE)
                     + ns_section(2, TPL_NS_SIZE, true);
    WriteConf(conf);
    INIT_SSD_CONFIG();
    ASSERT_EQ(1, device_count);
    EXPECT_EQ(TPL_NS_SIZE, devices[0].namespaces_size[0]);
    EXPECT_EQ(TPL_NS_SIZE, devices[0].namespaces_size[1]);
    EXPECT_EQ(16u, devices[0].ns_object_key_size[1]);
}

/* =========================================================================
 * 2. Namespace capacity validation tests
 * ========================================================================= */

TEST_F(SsdConfTest, CapacityFitsExact) {
    /* One namespace whose SIZE equals the full disk — no warning expected. */
    uint64_t exact = disk_bytes_from_template();
    WriteConf(device_header(1) + ns_section(1, exact));
    INIT_SSD_CONFIG();
    ASSERT_EQ(1, device_count);
    EXPECT_EQ(exact, ns_total(&devices[0]));
    EXPECT_LE(ns_total(&devices[0]), disk_bytes_from_template());
}

TEST_F(SsdConfTest, CapacityUnderfitSingleNamespace) {
    /* Template default: one namespace at 16 MB on a 640 MB disk. */
    WriteConf(device_header(1) + ns_section(1, TPL_NS_SIZE));
    INIT_SSD_CONFIG();
    ASSERT_EQ(1, device_count);
    uint64_t total_ns  = ns_total(&devices[0]);
    uint64_t total_disk = disk_bytes_from_template();
    EXPECT_LT(total_ns, total_disk) << "Expected underfit for template config";
    /* Utilization below 10% for a single 16 MB namespace on 640 MB disk. */
    double utilization = 100.0 * (double)total_ns / (double)total_disk;
    EXPECT_LT(utilization, 10.0);
}

TEST_F(SsdConfTest, CapacityUnderfitFourNamespaces) {
    /* Four namespaces (nvme06-like): 4 x 16 MB on a 640 MB disk. */
    std::string conf = device_header(1)
                     + ns_section(1, TPL_NS_SIZE, true)
                     + ns_section(2, TPL_NS_SIZE, true)
                     + ns_section(3, TPL_NS_SIZE, true)
                     + ns_section(4, TPL_NS_SIZE, true);
    WriteConf(conf);
    INIT_SSD_CONFIG();
    ASSERT_EQ(1, device_count);
    uint64_t total_ns   = ns_total(&devices[0]);
    uint64_t total_disk = disk_bytes_from_template();
    EXPECT_EQ(4 * TPL_NS_SIZE, total_ns);
    EXPECT_LT(total_ns, total_disk);
}

TEST_F(SsdConfTest, CapacityNamespaceSumFitsWithinDisk) {
    /* Verify the invariant: sum(ns SIZE) <= disk capacity for all devices
     * in the full 6-device template. */
    std::string conf;
    conf += device_header(1) + ns_section(1, TPL_NS_SIZE);
    conf += device_header(2, "2") + ns_section(1, TPL_NS_SIZE);
    conf += device_header(3, "3")
          + ns_section(1, TPL_NS_SIZE)
          + ns_section(2, TPL_NS_SIZE, true);
    conf += device_header(4, "4")
          + ns_section(1, TPL_NS_SIZE, true)
          + ns_section(2, TPL_NS_SIZE);
    conf += device_header(5, "5")
          + ns_section(1, TPL_NS_SIZE)
          + ns_section(2, TPL_NS_SIZE)
          + ns_section(3, TPL_NS_SIZE)
          + ns_section(4, TPL_NS_SIZE);
    conf += device_header(6, "6")
          + ns_section(1, TPL_NS_SIZE, true)
          + ns_section(2, TPL_NS_SIZE, true)
          + ns_section(3, TPL_NS_SIZE, true)
          + ns_section(4, TPL_NS_SIZE, true);
    WriteConf(conf);
    INIT_SSD_CONFIG();
    ASSERT_EQ(6, device_count);
    uint64_t disk = disk_bytes_from_template();
    for (int i = 0; i < device_count; i++) {
        uint64_t used = ns_total(&devices[i]);
        EXPECT_LE(used, disk)
            << "Device " << devices[i].device_name
            << ": namespace total " << used
            << " exceeds disk capacity " << disk;
    }
}

TEST_F(SsdConfTest, CapacityOverfitIsDetected) {
    /* A namespace whose SIZE exceeds the disk must be flagged.
     * INIT_SSD_CONFIG prints an error and returns without aborting in tests;
     * we verify device_count is 0 (init was aborted by RERR). */
    uint64_t oversize = disk_bytes_from_template() + 1;
    WriteConf(device_header(1) + ns_section(1, oversize));
    INIT_SSD_CONFIG();
    /*
     * RERR(,) returns void — the function unwinds without setting device_count.
     * A device_count of 0 (or unchanged from before INIT) confirms the error
     * path was triggered.
     */
    EXPECT_EQ(0, device_count);
}

/* ---------------------------------------------------------------------------
 * ONFI manager-threads / config-field tests (SSDConfigTest, BaseTest-based).
 * Complements the [nsXX] parsing + capacity validation tests above.
 * ------------------------------------------------------------------------- */
namespace ssd_conf_tests {

    class SSDConfigTest : public BaseTest {
        public:
            virtual void SetUp() {
                BaseTest::SetUp();
                INIT_LOG_MANAGER(g_device_index);
            }

            virtual void TearDown() {
                BaseTest::TearDown(false);
                TERM_LOG_MANAGER(g_device_index);
                TERM_SSD_CONFIG();
            }
    };

    // Test parameter generation
    std::vector<SSDConf*> GetTestParams() {
        std::vector<SSDConf*> ssd_configs;
        // Add configurations with different parameters
        int onfi_multithreaded_options[] = {1, 7, 0, -5};
        int sector_size = 1;

        for (int onfi_multithreaded : onfi_multithreaded_options)
            ssd_configs.push_back(new SSDConf(parameters::Allsizemb[0], sector_size, onfi_multithreaded));
        return ssd_configs;
    }

    INSTANTIATE_TEST_CASE_P(DiskSize, SSDConfigTest, ::testing::ValuesIn(GetTestParams()));

    TEST_P(SSDConfigTest, ParseBasicConfigValues) {
        // Verify that parsed values match the serialized config
        SSDConf* ssd_config = base_test_get_ssd_config();

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

        // Validate ONFI multithreaded flag
        int expected_onfi_multithreaded = ssd_config->get_onfi_multithreaded();
        if (expected_onfi_multithreaded < 0) {
            expected_onfi_multithreaded = 0;
        }
        ASSERT_EQ(expected_onfi_multithreaded, devices[g_device_index].onfi_multithreaded);
        ASSERT_EQ(ssd_config->get_onfi_manager_queue_size(), devices[g_device_index].onfi_manager_queue_size);

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

    TEST_P(SSDConfigTest, VerifyNamespaceCapacity) {
        // Verifies that the total allocated namespace capacity does not exceed the disk's capacity.
        SSDConf* ssd_config = base_test_get_ssd_config();
        uint64_t total_disk_blocks = ssd_config->get_block_nb();
        uint64_t total_namespace_blocks = 0;

        for (int i = 0; i < MAX_NUMBER_OF_NAMESPACES; i++) {
            total_namespace_blocks += devices[g_device_index].namespaces_size[i];
        }

        if (total_namespace_blocks < total_disk_blocks) {
            std::cerr << "WARNING: Total namespace capacity (" << total_namespace_blocks
                      << " blocks) is less than disk capacity (" << total_disk_blocks
                      << " blocks). " << (total_disk_blocks - total_namespace_blocks)
                      << " blocks unused." << std::endl;
        }

        ASSERT_LE(total_namespace_blocks, total_disk_blocks)
            << "Configuration Error: Total namespace size (" << total_namespace_blocks
            << ") exceeds available disk capacity (" << total_disk_blocks << ").";
    }

} //namespace
