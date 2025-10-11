#include "base_emulator_tests.h"
#include <thread>
#include <atomic>
#include <math.h>
#include <assert.h>
#include <typeinfo>
#include <vector>

extern "C" {
#include "common.h"
#include "ftl_obj_strategy.h"
#include "uthash.h"

#include "osd.h"
#include "osd-util/osd-util.h"
#include "osd-util/osd-defs.h"
}

using namespace std;

namespace multi_device_tests {

    class MultiDeviceWorkloadTest : public BaseTest {
        public:
            virtual void SetUp() {
                BaseTest::SetUp();

                INIT_LOG_MANAGER(g_device_index);

                // Initialize all secondary devices from the parsed config
                for (uint8_t i = 0; i < device_count; i++) {
                    if (i != g_device_index) {
                        FTL_INIT(i);
                        INIT_LOG_MANAGER(i);
                    }
                }
            }

            virtual void TearDown() {
                // Clean up secondary devices FIRST (not the primary one)
                for (uint8_t i = 0; i < device_count; i++) {
                    if (i != g_device_index) {
                        FTL_TERM(i);
                        TERM_LOG_MANAGER(i);
                        std::ignore = system((std::string("rm -rf data/") + std::to_string(i)).c_str());
                    }
                }

                BaseTest::TearDown(false);
                TERM_LOG_MANAGER(g_device_index);
                TERM_SSD_CONFIG();
            }
    };

    std::vector<SSDConf*> GetTestParams() {
        std::vector<SSDConf*> ssd_configs;

        ssd_configs.push_back(new SSDConf(1));
        return ssd_configs;
    }

    INSTANTIATE_TEST_CASE_P(Workloads, MultiDeviceWorkloadTest,
                            ::testing::ValuesIn(GetTestParams()));

    /**
     * Test: Device Isolation
     * Scenario: Write different patterns to each device and verify they don't interfere
     */
    TEST_P(MultiDeviceWorkloadTest, DeviceIsolation) {
        SSDConf* ssd_config = base_test_get_ssd_config();
        size_t page_size = ssd_config->get_page_size();
        size_t pages_to_test = std::min(size_t(10), ssd_config->get_pages());
        ASSERT_GE(device_count, 3);

        std::vector<size_t> device2_written_pages;
        device2_written_pages.reserve(pages_to_test);

        // Device 0: Sequential writes starting at page 0
        for(size_t p = 0; p < pages_to_test; p++) {
            ASSERT_EQ(FTL_SUCCESS, FTL_WRITE_SECT(0, p * page_size, 1, NULL))
                << "Device 0 sequential write failed at page " << p;
        }

        // Device 1: Sequential writes starting at page 0 (different device!)
        for(size_t p = 0; p < pages_to_test; p++) {
            ASSERT_EQ(FTL_SUCCESS, FTL_WRITE_SECT(1, p * page_size, 1, NULL))
                << "Device 1 sequential write failed at page " << p;
        }

        // Device 2: Random writes
        for(size_t i = 0; i < pages_to_test; i++) {
            size_t page = rand() % ssd_config->get_pages();
            device2_written_pages.push_back(page);
            ASSERT_EQ(FTL_SUCCESS, FTL_WRITE_SECT(2, page * page_size, 1, NULL))
                << "Device 2 random write failed";
        }

        // Verify: Devices 0 and 1 can still read their sequential data.
        for(size_t p = 0; p < pages_to_test; p++) {
            ASSERT_EQ(FTL_SUCCESS, FTL_READ_SECT(0, p * page_size, 1, NULL))
                << "Device 0 read failed after multi-device writes";
            ASSERT_EQ(FTL_SUCCESS, FTL_READ_SECT(1, p * page_size, 1, NULL))
                << "Device 1 read failed after multi-device writes";
        }

        // Verify: Device 2 can read all random pages that were written.
        for (size_t i = 0; i < device2_written_pages.size(); i++) {
            ASSERT_EQ(FTL_SUCCESS, FTL_READ_SECT(2, device2_written_pages[i] * page_size, 1, NULL))
                << "Device 2 read failed after random writes at page " << device2_written_pages[i];
        }
    }

    TEST_P(MultiDeviceWorkloadTest, DeviceIndexBoundaryWriteFailsGracefully) {
        ASSERT_GT(device_count, 0);

        uint8_t invalid_device_index = device_count;
        ASSERT_EQ(FTL_FAILURE, FTL_WRITE_SECT(invalid_device_index, 0, 1, NULL))
            << "Invalid device index should fail cleanly";
    }

    /**
     * Test: Database + Log Workload
    * Scenario: Primary device = database (random writes), one secondary device = logs (sequential writes)
     */
    TEST_P(MultiDeviceWorkloadTest, DatabaseAndLogWorkload) {
        SSDConf* ssd_config = base_test_get_ssd_config();
        size_t page_size = ssd_config->get_page_size();
        size_t operations = std::min(size_t(50), ssd_config->get_pages());

        // Simulate interleaved database and log operations
        for(size_t i = 0; i < operations; i++) {
            // Database: Random write to device 0
            size_t db_page = rand() % ssd_config->get_pages();
            ASSERT_EQ(FTL_SUCCESS, FTL_WRITE_SECT(0, db_page * page_size, 1, NULL))
                << "Database write failed at iteration " << i;

            // Log: Sequential append to device 1
            if (i < ssd_config->get_pages()) {
                ASSERT_EQ(FTL_SUCCESS, FTL_WRITE_SECT(1, i * page_size, 1, NULL))
                    << "Log append failed at iteration " << i;
            }

            // Every 10 operations, do a database read
            if (i % 10 == 0 && i > 0) {
                size_t read_page = rand() % ssd_config->get_pages();
                FTL_READ_SECT(0, read_page * page_size, 1, NULL);
            }
        }
    }

    /**
     * Test: Parallel Write-Then-Read Across Devices
     * Scenario: Run parallel writes across all devices, then validate with parallel reads.
     */
    TEST_P(MultiDeviceWorkloadTest, ParallelWriteThenReadAcrossDevices) {
        SSDConf* ssd_config = base_test_get_ssd_config();
        size_t page_size = ssd_config->get_page_size();
        size_t pages_to_test = std::min(size_t(20), ssd_config->get_pages());
        ASSERT_GT(device_count, 0);

        std::atomic<int> error_count(0);

        auto write_worker = [&](uint8_t device_id) {
            for (size_t p = 0; p < pages_to_test; p++) {
                if (FTL_WRITE_SECT(device_id, p * page_size, 1, NULL) != FTL_SUCCESS) {
                    error_count++;
                    return;
                }
            }
        };

        // Phase 1: parallel writes.
        {
            std::vector<std::thread> threads;
            for (uint8_t i = 0; i < device_count; i++) {
                threads.push_back(std::thread(write_worker, i));
            }

            for (size_t i = 0; i < threads.size(); i++) {
                threads[i].join();
            }

            ASSERT_EQ(0, error_count.load())
                << "Parallel write phase failed on one or more devices";
        }

        error_count = 0;
        auto read_worker = [&](uint8_t device_id) {
            for (size_t p = 0; p < pages_to_test; p++) {
                if (FTL_READ_SECT(device_id, p * page_size, 1, NULL) != FTL_SUCCESS) {
                    error_count++;
                    return;
                }
            }
        };

        // Phase 2: parallel reads from the same logical pages.
        {
            std::vector<std::thread> threads;
            for (uint8_t i = 0; i < device_count; i++) {
                threads.push_back(std::thread(read_worker, i));
            }

            for (size_t i = 0; i < threads.size(); i++) {
                threads[i].join();
            }
        }

        ASSERT_EQ(0, error_count.load())
            << "Parallel read phase failed on one or more devices";
    }

    /**
     * ===========================
     * OBJECT STRATEGY TESTS
     * ===========================
     */

    class MultiDeviceObjectTest : public BaseTest {
        protected:
            std::vector<int> object_size_;
            std::vector<unsigned int> objects_in_ssd_;

        public:
            virtual void SetUp() {
                BaseTest::SetUp();

                SSDConf* ssd_config = base_test_get_ssd_config();

                object_size_.assign(device_count, 0);
                objects_in_ssd_.assign(device_count, 0);

                object_size_[g_device_index] = ssd_config->get_object_size();
                int object_pages = (int)ceil(1.0 * object_size_[g_device_index] / GET_PAGE_SIZE(g_device_index));
                objects_in_ssd_[g_device_index] = (unsigned int)((devices[g_device_index].pages_in_ssd - devices[g_device_index].page_nb) / object_pages);

                INIT_LOG_MANAGER(g_device_index);

                for (uint8_t i = 0; i < device_count; i++) {
                    if (i != g_device_index) {
                        FTL_INIT(i);
                        object_size_[i] = ssd_config->get_object_size();
                        int obj_pages = (int)ceil(1.0 * object_size_[i] / GET_PAGE_SIZE(i));
                        objects_in_ssd_[i] = (unsigned int)((devices[i].pages_in_ssd - devices[i].page_nb) / obj_pages);
                        INIT_LOG_MANAGER(i);
                    }
                }
            }

            virtual void TearDown() {
                // Clean up secondary devices FIRST (not the primary one)
                for (uint8_t i = 0; i < device_count; i++) {
                    if (i != g_device_index) {
                        FTL_TERM(i);
                        TERM_LOG_MANAGER(i);
                        std::ignore = system((std::string("rm -rf data/") + std::to_string(i)).c_str());
                    }
                }

                BaseTest::TearDown(false);
                TERM_LOG_MANAGER(g_device_index);
                TERM_SSD_CONFIG();
            }
    };

    std::vector<SSDConf*> GetObjectTestParams() {
        std::vector<SSDConf*> ssd_configs;

        size_t page_size = 4096;
        size_t sector_size = 1;
        size_t page_nb = 10;
        size_t block_nb = 128;

        for (unsigned int i = 0; i < BASE_TEST_ARRAY_SIZE(parameters::Allobjsize); i++) {
            SSDConf* config = new SSDConf(
                page_size, page_nb, sector_size, DEFAULT_FLASH_NB, block_nb, DEFAULT_FLASH_NB);
            config->set_object_size(parameters::Allobjsize[i]);
            config->set_storage_strategy(STRATEGY_OBJECT);
            ssd_configs.push_back(config);
        }

        return ssd_configs;
    }

    INSTANTIATE_TEST_CASE_P(ObjectWorkloads, MultiDeviceObjectTest,
                            ::testing::ValuesIn(GetObjectTestParams()));

    /**
     * Test: Parallel Object Create Tests
      * Scenario: Create objects on all configured devices simultaneously
     */
    TEST_P(MultiDeviceObjectTest, ParallelObjectCreateTests) {
        std::atomic<int> error_count(0);

        auto object_create_worker = [&](uint8_t device_id) {
            unsigned int objects_to_create = objects_in_ssd_[device_id] / 2;
            for (unsigned long p = 1; p <= objects_to_create; p++) {
                obj_id_t object_loc = { .object_id = USEROBJECT_OID_LB + p, .partition_id = USEROBJECT_PID_LB };
                if (!FTL_OBJ_CREATE(device_id, object_loc, object_size_[device_id])) {
                    error_count++;
                    return;
                }
            }
        };

        // Test: Parallel object creation on all configured devices
        {
            std::vector<std::thread> threads;
            for (uint8_t i = 0; i < device_count; i++) {
                threads.push_back(std::thread(object_create_worker, i));
            }

            for (size_t i = 0; i < threads.size(); i++) {
                threads[i].join();
            }

            ASSERT_EQ(0, error_count.load())
                << "Parallel object creation failed on one or more devices";
        }
    }

    TEST_P(MultiDeviceObjectTest, ObjectIsolationAcrossDevicesSameObjectId) {
        ASSERT_GE(device_count, 3);

        uint8_t first_device = INVALID_DEVICE_INDEX;
        uint8_t second_device = INVALID_DEVICE_INDEX;
        for (uint8_t i = 0; i < device_count; i++) {
            if (i == g_device_index) {
                continue;
            }

            if (first_device == INVALID_DEVICE_INDEX) {
                first_device = i;
            } else {
                second_device = i;
                break;
            }
        }

        ASSERT_NE(INVALID_DEVICE_INDEX, first_device);
        ASSERT_NE(INVALID_DEVICE_INDEX, second_device);

        obj_id_t object_loc = { .object_id = USEROBJECT_OID_LB + 1, .partition_id = USEROBJECT_PID_LB };
        const uint32_t payload_length = 64;

        char first_payload[payload_length + 1];
        char second_payload[payload_length + 1];
        memset(first_payload, 0x0, sizeof(first_payload));
        memset(second_payload, 0x0, sizeof(second_payload));
        snprintf(first_payload, sizeof(first_payload), "first_dev_%u", (unsigned int)first_device);
        snprintf(second_payload, sizeof(second_payload), "second_dev_%u", (unsigned int)second_device);

        ASSERT_TRUE(FTL_OBJ_CREATE(first_device, object_loc, object_size_[first_device]))
            << "Failed to create object on first device";
        ASSERT_TRUE(FTL_OBJ_CREATE(second_device, object_loc, object_size_[second_device]))
            << "Failed to create object on second device";

        ASSERT_EQ(FTL_SUCCESS, FTL_OBJ_WRITE(first_device, object_loc, first_payload, 0, payload_length))
            << "Failed to write object on first device";
        ASSERT_EQ(FTL_SUCCESS, FTL_OBJ_WRITE(second_device, object_loc, second_payload, 0, payload_length))
            << "Failed to write object on second device";

        char first_readback[payload_length + 1];
        char second_readback[payload_length + 1];
        memset(first_readback, 0x0, sizeof(first_readback));
        memset(second_readback, 0x0, sizeof(second_readback));

        uint32_t first_len = payload_length;
        uint32_t second_len = payload_length;
        ASSERT_EQ(FTL_SUCCESS, FTL_OBJ_READ(first_device, object_loc, first_readback, 0, &first_len))
            << "Failed to read object from first device";
        ASSERT_EQ(FTL_SUCCESS, FTL_OBJ_READ(second_device, object_loc, second_readback, 0, &second_len))
            << "Failed to read object from second device";

        ASSERT_STREQ(first_payload, first_readback);
        ASSERT_STREQ(second_payload, second_readback);
        ASSERT_STRNE(first_readback, second_readback)
            << "Same object_id across devices should not alias the same data";
    }

    /**
     * Test: Parallel Object Create-Write Tests
      * Scenario: Create and write objects on all configured devices simultaneously
     */
    TEST_P(MultiDeviceObjectTest, ParallelObjectCreateWriteTests) {
        std::atomic<int> error_count(0);

        auto object_create_write_worker = [&](uint8_t device_id) {
            unsigned int objects_to_create = objects_in_ssd_[device_id] / 2;
            char *wrbuf = (char *)calloc(1, GET_PAGE_SIZE(device_id));
            if (wrbuf == NULL) {
                error_count++;
                return;
            }

            for (unsigned long p = 1; p <= objects_to_create; p++) {
                obj_id_t object_loc = { .object_id = USEROBJECT_OID_LB + p, .partition_id = USEROBJECT_PID_LB };

                if (!FTL_OBJ_CREATE(device_id, object_loc, object_size_[device_id])) {
                    error_count++;
                    free(wrbuf);
                    return;
                }

                if (FTL_OBJ_WRITE(device_id, object_loc, wrbuf, 0, GET_PAGE_SIZE(device_id)) != FTL_SUCCESS) {
                    error_count++;
                    free(wrbuf);
                    return;
                }
            }

            free(wrbuf);
        };

        {
            std::vector<std::thread> threads;
            for (uint8_t i = 0; i < device_count; i++) {
                threads.push_back(std::thread(object_create_write_worker, i));
            }

            for (size_t i = 0; i < threads.size(); i++) {
                threads[i].join();
            }

            ASSERT_EQ(0, error_count.load())
                << "Parallel object create-write failed on one or more devices";
        }
    }

    /**
     * Test: Parallel Object Create-Write-Read Tests
      * Scenario: Create, write, and read objects on all configured devices simultaneously
     */
    TEST_P(MultiDeviceObjectTest, ParallelObjectCreateWriteReadTests) {
        std::atomic<int> error_count(0);

        auto object_create_write_read_worker = [&](uint8_t device_id) {
            unsigned int objects_to_create = objects_in_ssd_[device_id] / 2;
            char *wrbuf = (char *)calloc(1, object_size_[device_id]);
            char *rdbuf = (char *)calloc(1, GET_PAGE_SIZE(device_id) / 2);

            if (wrbuf == NULL || rdbuf == NULL) {
                error_count++;
                free(wrbuf);
                free(rdbuf);
                return;
            }

            // Create and write phase
            for (unsigned long p = 1; p <= objects_to_create; p++) {
                obj_id_t object_loc = { .object_id = USEROBJECT_OID_LB + p, .partition_id = USEROBJECT_PID_LB };

                if (!FTL_OBJ_CREATE(device_id, object_loc, object_size_[device_id])) {
                    error_count++;
                    free(wrbuf);
                    free(rdbuf);
                    return;
                }

                memset(wrbuf, 0x0, object_size_[device_id]);
                sprintf(wrbuf, "dev%d_obj%lu", device_id, object_loc.object_id);

                if (FTL_OBJ_WRITE(device_id, object_loc, wrbuf, 0, object_size_[device_id]) != FTL_SUCCESS) {
                    error_count++;
                    free(wrbuf);
                    free(rdbuf);
                    return;
                }
            }

            // Read and verify phase
            for (unsigned long p = 1; p <= objects_to_create; p++) {
                obj_id_t object_loc = { .object_id = USEROBJECT_OID_LB + p, .partition_id = USEROBJECT_PID_LB };
                uint32_t len = GET_PAGE_SIZE(device_id) / 2;

                memset(rdbuf, 0x0, GET_PAGE_SIZE(device_id) / 2);

                if (FTL_OBJ_READ(device_id, object_loc, rdbuf, 0, &len) != FTL_SUCCESS) {
                    error_count++;
                    free(wrbuf);
                    free(rdbuf);
                    return;
                }

                char expected[256];
                sprintf(expected, "dev%d_obj%lu", device_id, object_loc.object_id);

                if (strcmp(rdbuf, expected) != 0) {
                    error_count++;
                    free(wrbuf);
                    free(rdbuf);
                    return;
                }
            }

            free(wrbuf);
            free(rdbuf);
        };

        // Test: Parallel object create-write-read on all configured devices
        {
            std::vector<std::thread> threads;
            for (uint8_t i = 0; i < device_count; i++) {
                threads.push_back(std::thread(object_create_write_read_worker, i));
            }

            for (size_t i = 0; i < threads.size(); i++) {
                threads[i].join();
            }

            ASSERT_EQ(0, error_count.load())
                << "Parallel object create-write-read failed on one or more devices";
        }
    }

    /**
     * ===========================
     * MIXED STRATEGY TESTS
     * ===========================
     */

    class MultiDeviceMixedTest : public BaseTest {
        protected:
            std::vector<int> object_size_;
            std::vector<unsigned int> objects_in_ssd_;

        public:
            virtual void SetUp() {
                BaseTest::SetUp();
                SSDConf* ssd_config = base_test_get_ssd_config();

                object_size_.assign(device_count, 0);
                objects_in_ssd_.assign(device_count, 0);

                // Keep primary device in sector strategy.
                INIT_LOG_MANAGER(g_device_index);

                // Configure all secondary devices with object strategy.
                for (uint8_t i = 0; i < device_count; i++) {
                    if (i == g_device_index) {
                        continue;
                    }

                    devices[i].storage_strategy = STRATEGY_OBJECT;
                    FTL_INIT(i);
                    object_size_[i] = ssd_config->get_object_size();
                    int object_pages = (int)ceil(1.0 * object_size_[i] / GET_PAGE_SIZE(i));
                    objects_in_ssd_[i] = (unsigned int)((devices[i].pages_in_ssd - devices[i].page_nb) / object_pages);
                    INIT_LOG_MANAGER(i);
                }
            }

            virtual void TearDown() {
                // Clean up secondary object devices first.
                for (uint8_t i = 0; i < device_count; i++) {
                    if (i == g_device_index) {
                        continue;
                    }

                    FTL_TERM(i);
                    TERM_LOG_MANAGER(i);
                    std::ignore = system((std::string("rm -rf data/") + std::to_string(i)).c_str());
                }

                // Clean up primary device (sector)
                BaseTest::TearDown(false);
                TERM_LOG_MANAGER(g_device_index);
                TERM_SSD_CONFIG();
            }
    };

    std::vector<SSDConf*> GetMixedTestParams() {
        std::vector<SSDConf*> ssd_configs;

        ssd_configs.push_back(new SSDConf(1));
        return ssd_configs;
    }

    INSTANTIATE_TEST_CASE_P(MixedWorkloads, MultiDeviceMixedTest,
                            ::testing::ValuesIn(GetMixedTestParams()));

    /**
     * Test: Mixed Strategy Isolation
      * Scenario: Primary device = sector, secondary devices = object
     * Verify each device can operate with its own strategy independently
     */
    TEST_P(MultiDeviceMixedTest, MixedStrategyIsolation) {
        SSDConf* ssd_config = base_test_get_ssd_config();
        size_t page_size = ssd_config->get_page_size();
        ASSERT_GE(device_count, 2);

        uint8_t sector_device = g_device_index;

        // Sector writes on primary device.
        for(size_t p = 0; p < 10; p++) {
            ASSERT_EQ(FTL_SUCCESS, FTL_WRITE_SECT(sector_device, p * page_size, 1, NULL))
                << "Primary device (sector) write failed at page " << p;
        }

        // Object creates and writes on all secondary devices.
        for (uint8_t device_id = 0; device_id < device_count; device_id++) {
            if (device_id == sector_device) {
                continue;
            }

            for (unsigned long p = 1; p <= 5; p++) {
                obj_id_t object_loc = { .object_id = USEROBJECT_OID_LB + p, .partition_id = USEROBJECT_PID_LB };
                ASSERT_TRUE(FTL_OBJ_CREATE(device_id, object_loc, object_size_[device_id]))
                    << "Device " << (unsigned int)device_id << " (object) create failed";
            }
        }

        // Verify primary sector device can still read.
        ASSERT_EQ(FTL_SUCCESS, FTL_READ_SECT(sector_device, 0, 1, NULL))
            << "Primary device (sector) read failed after mixed operations";
    }

    /**
     * Test: Parallel Mixed Strategy Operations
      * Scenario: Primary device does sector I/O while secondary devices do object I/O simultaneously
     */
    TEST_P(MultiDeviceMixedTest, ParallelMixedStrategyOperations) {
        SSDConf* ssd_config = base_test_get_ssd_config();
        size_t page_size = ssd_config->get_page_size();
        std::atomic<int> error_count(0);
        ASSERT_GE(device_count, 2);

        uint8_t sector_device = g_device_index;

        // Primary sector worker.
        auto sector_worker = [&]() {
            size_t total_pages = (ssd_config->get_pages() * 4) / 5;
            for(size_t p = 0; p < total_pages; p++) {
                if (FTL_WRITE_SECT(sector_device, p * page_size, 1, NULL) != FTL_SUCCESS) {
                    error_count++;
                    return;
                }
            }
        };

        // Object workers on all secondary devices.
        auto object_worker = [&](uint8_t device_id) {
            unsigned int objects_to_create = objects_in_ssd_[device_id] / 2;
            char *wrbuf = (char *)calloc(1, object_size_[device_id]);
            if (wrbuf == NULL) {
                error_count++;
                return;
            }

            for (unsigned long p = 1; p <= objects_to_create; p++) {
                obj_id_t object_loc = { .object_id = USEROBJECT_OID_LB + p, .partition_id = USEROBJECT_PID_LB };

                if (!FTL_OBJ_CREATE(device_id, object_loc, object_size_[device_id])) {
                    error_count++;
                    free(wrbuf);
                    return;
                }

                memset(wrbuf, 0x0, object_size_[device_id]);
                sprintf(wrbuf, "dev%d_obj%lu", device_id, object_loc.object_id);

                if (FTL_OBJ_WRITE(device_id, object_loc, wrbuf, 0, object_size_[device_id]) != FTL_SUCCESS) {
                    error_count++;
                    free(wrbuf);
                    return;
                }
            }

            free(wrbuf);
        };

        {
            std::thread sector_thread(sector_worker);
            std::vector<std::thread> object_threads;
            for (uint8_t i = 0; i < device_count; i++) {
                if (i != sector_device) {
                    object_threads.push_back(std::thread(object_worker, i));
                }
            }

            sector_thread.join();
            for (size_t i = 0; i < object_threads.size(); i++) {
                object_threads[i].join();
            }

            ASSERT_EQ(0, error_count.load())
                << "Parallel mixed strategy operations failed";
        }
    }

} //namespace
