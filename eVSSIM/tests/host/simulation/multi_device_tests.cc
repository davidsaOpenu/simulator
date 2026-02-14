#include "base_emulator_tests.h"
#include <thread>
#include <atomic>
#include <math.h>
#include <assert.h>
#include <typeinfo>

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
        protected:
            bool device_initialized[3] = {false, false, false};

        public:
            virtual void SetUp() {
                BaseTest::SetUp();

                INIT_LOG_MANAGER(g_device_index);
                device_initialized[g_device_index] = true;

                // Initialize the other 2 devices
                for (uint8_t i = 0; i < 3; i++) {
                    if (i != g_device_index && !device_initialized[i]) {
                        FTL_INIT(i);
                        INIT_LOG_MANAGER(i);
                        device_initialized[i] = true;
                    }
                }
            }

            virtual void TearDown() {
                // Clean up secondary devices FIRST (not the primary one)
                for (uint8_t i = 0; i < 3; i++) {
                    if (i != g_device_index && device_initialized[i]) {
                        FTL_TERM(i);
                        TERM_LOG_MANAGER(i);
                        std::ignore = system((std::string("rm -rf data/") + std::to_string(i)).c_str());
                        device_initialized[i] = false;
                    }
                }

                BaseTest::TearDown(false);
                TERM_LOG_MANAGER(g_device_index);
                TERM_SSD_CONFIG();
                device_initialized[g_device_index] = false;
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
            ASSERT_EQ(FTL_SUCCESS, FTL_WRITE_SECT(2, page * page_size, 1, NULL))
                << "Device 2 random write failed";
        }

        // Verify: All devices can still read their data
        for(size_t p = 0; p < pages_to_test; p++) {
            ASSERT_EQ(FTL_SUCCESS, FTL_READ_SECT(0, p * page_size, 1, NULL))
                << "Device 0 read failed after multi-device writes";
            ASSERT_EQ(FTL_SUCCESS, FTL_READ_SECT(1, p * page_size, 1, NULL))
                << "Device 1 read failed after multi-device writes";
        }
    }

    /**
     * Test: Database + Log Workload
     * Scenario: Device 0 = database (random writes), Device 1 = logs (sequential writes)
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
     * Test: Parallel Sector Tests
     * Scenario: Run the same sector test workload on all 3 devices simultaneously.
     */
    TEST_P(MultiDeviceWorkloadTest, ParallelSectorTests) {
        SSDConf* ssd_config = base_test_get_ssd_config();
        size_t page_size = ssd_config->get_page_size();
        // Calculate writable pages: total pages minus reserved blocks for GC and over-provisioning
        // Each device reserves at least one block (page_nb pages) for GC
        // Safe limit is about 80% of total pages to avoid running out of empty blocks
        size_t total_pages = (ssd_config->get_pages() * 4) / 5;  // Use 80% of available pages

        // Shared error tracking (atomic for thread safety)
        std::atomic<int> error_count(0);

        auto sequential_write_worker = [&](uint8_t device_id) {
            for(size_t p = 0; p < total_pages; p++) {
                if (FTL_WRITE_SECT(device_id, p * page_size, 1, NULL) != FTL_SUCCESS) {
                    error_count++;
                    return;
                }
            }
        };

        auto random_write_worker = [&](uint8_t device_id) {
            for(size_t i = 0; i < total_pages; i++) {
                size_t random_page = rand() % total_pages;
                if (FTL_WRITE_SECT(device_id, random_page * page_size, 1, NULL) != FTL_SUCCESS) {
                    error_count++;
                    return;
                }
            }
        };

        auto mixed_workload_worker = [&](uint8_t device_id) {
            // Phase 1: Random writes
            for(size_t p = 0; p < total_pages; p++) {
                size_t random_page = rand() % total_pages;
                if (FTL_WRITE_SECT(device_id, random_page * page_size, 1, NULL) != FTL_SUCCESS) {
                    error_count++;
                    return;
                }
            }
            // Phase 2: Sequential writes
            for(size_t p = 0; p < total_pages; p++) {
                if (FTL_WRITE_SECT(device_id, p * page_size, 1, NULL) != FTL_SUCCESS) {
                    error_count++;
                    return;
                }
            }
        };

        // Test 1: Parallel sequential writes on all 3 devices
        {
            std::thread t0(sequential_write_worker, 0);
            std::thread t1(sequential_write_worker, 1);
            std::thread t2(sequential_write_worker, 2);

            t0.join();
            t1.join();
            t2.join();

            ASSERT_EQ(0, error_count.load())
                << "Parallel sequential writes failed on one or more devices";
            error_count = 0;
        }

        // Test 2: Parallel random writes on all 3 devices
        {
            std::thread t0(random_write_worker, 0);
            std::thread t1(random_write_worker, 1);
            std::thread t2(random_write_worker, 2);

            t0.join();
            t1.join();
            t2.join();

            ASSERT_EQ(0, error_count.load())
                << "Parallel random writes failed on one or more devices";
            error_count = 0;
        }

        // Test 3: Parallel mixed workload on all 3 devices
        {
            std::thread t0(mixed_workload_worker, 0);
            std::thread t1(mixed_workload_worker, 1);
            std::thread t2(mixed_workload_worker, 2);

            t0.join();
            t1.join();
            t2.join();

            ASSERT_EQ(0, error_count.load())
                << "Parallel mixed workload failed on one or more devices";
        }

        // Verify all devices can still be read
        ASSERT_EQ(FTL_SUCCESS, FTL_READ_SECT(0, 0, 1, NULL))
            << "Device 0 read failed after parallel tests";
        ASSERT_EQ(FTL_SUCCESS, FTL_READ_SECT(1, 0, 1, NULL))
            << "Device 1 read failed after parallel tests";
        ASSERT_EQ(FTL_SUCCESS, FTL_READ_SECT(2, 0, 1, NULL))
            << "Device 2 read failed after parallel tests";
    }

    /**
     * ===========================
     * OBJECT STRATEGY TESTS
     * ===========================
     */

    class MultiDeviceObjectTest : public BaseTest {
        protected:
            bool device_initialized[3] = {false, false, false};
            int object_size_[3] = {0, 0, 0};
            unsigned int objects_in_ssd_[3] = {0, 0, 0};

        public:
            virtual void SetUp() {
                BaseTest::SetUp();

                SSDConf* ssd_config = base_test_get_ssd_config();

                object_size_[g_device_index] = ssd_config->get_object_size();
                int object_pages = (int)ceil(1.0 * object_size_[g_device_index] / GET_PAGE_SIZE(g_device_index));
                objects_in_ssd_[g_device_index] = (unsigned int)((devices[g_device_index].pages_in_ssd - devices[g_device_index].page_nb) / object_pages);

                INIT_LOG_MANAGER(g_device_index);
                device_initialized[g_device_index] = true;

                for (uint8_t i = 0; i < 3; i++) {
                    if (i != g_device_index && !device_initialized[i]) {
                        FTL_INIT(i);
                        object_size_[i] = ssd_config->get_object_size();
                        int obj_pages = (int)ceil(1.0 * object_size_[i] / GET_PAGE_SIZE(i));
                        objects_in_ssd_[i] = (unsigned int)((devices[i].pages_in_ssd - devices[i].page_nb) / obj_pages);
                        INIT_LOG_MANAGER(i);
                        device_initialized[i] = true;
                    }
                }
            }

            virtual void TearDown() {
                // Clean up secondary devices FIRST (not the primary one)
                for (uint8_t i = 0; i < 3; i++) {
                    if (i != g_device_index && device_initialized[i]) {
                        FTL_TERM(i);
                        TERM_LOG_MANAGER(i);
                        std::ignore = system((std::string("rm -rf data/") + std::to_string(i)).c_str());
                        device_initialized[i] = false;
                    }
                }

                BaseTest::TearDown(false);
                TERM_LOG_MANAGER(g_device_index);
                TERM_SSD_CONFIG();
                device_initialized[g_device_index] = false;
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
     * Scenario: Create objects on all 3 devices simultaneously
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

        // Test: Parallel object creation on all 3 devices
        {
            std::thread t0(object_create_worker, 0);
            std::thread t1(object_create_worker, 1);
            std::thread t2(object_create_worker, 2);

            t0.join();
            t1.join();
            t2.join();

            ASSERT_EQ(0, error_count.load())
                << "Parallel object creation failed on one or more devices";
        }
    }

    /**
     * Test: Parallel Object Create-Write Tests
     * Scenario: Create and write objects on all 3 devices simultaneously
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
            std::thread t0(object_create_write_worker, 0);
            std::thread t1(object_create_write_worker, 1);
            std::thread t2(object_create_write_worker, 2);

            t0.join();
            t1.join();
            t2.join();

            ASSERT_EQ(0, error_count.load())
                << "Parallel object create-write failed on one or more devices";
        }
    }

    /**
     * Test: Parallel Object Create-Write-Read Tests
     * Scenario: Create, write, and read objects on all 3 devices simultaneously
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

        // Test: Parallel object create-write-read on all 3 devices
        {
            std::thread t0(object_create_write_read_worker, 0);
            std::thread t1(object_create_write_read_worker, 1);
            std::thread t2(object_create_write_read_worker, 2);

            t0.join();
            t1.join();
            t2.join();

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
            bool device_initialized[3] = {false, false, false};
            int object_size_[3] = {0, 0, 0};
            unsigned int objects_in_ssd_[3] = {0, 0, 0};

        public:
            virtual void SetUp() {
                BaseTest::SetUp();
                SSDConf* ssd_config = base_test_get_ssd_config();

                // Device 0: Sector strategy
                INIT_LOG_MANAGER(g_device_index);
                device_initialized[g_device_index] = true;

                // Device 1: Object strategy
                devices[1].storage_strategy = STRATEGY_OBJECT;
                FTL_INIT(1);
                object_size_[1] = ssd_config->get_object_size();
                int object_pages = (int)ceil(1.0 * object_size_[1] / GET_PAGE_SIZE(1));
                objects_in_ssd_[1] = (unsigned int)((devices[1].pages_in_ssd - devices[1].page_nb) / object_pages);
                INIT_LOG_MANAGER(1);
                device_initialized[1] = true;

                // Device 2: Object strategy
                devices[2].storage_strategy = STRATEGY_OBJECT;
                FTL_INIT(2);
                object_size_[2] = ssd_config->get_object_size();
                int object_pages_2 = (int)ceil(1.0 * object_size_[2] / GET_PAGE_SIZE(2));
                objects_in_ssd_[2] = (unsigned int)((devices[2].pages_in_ssd - devices[2].page_nb) / object_pages_2);
                INIT_LOG_MANAGER(2);
                device_initialized[2] = true;
            }

            virtual void TearDown() {
                // Clean up device 2 (object)
                if (device_initialized[2]) {
                    FTL_TERM(2);
                    TERM_LOG_MANAGER(2);
                    std::ignore = system("rm -rf data/2");
                    device_initialized[2] = false;
                }

                // Clean up device 1 (object)
                if (device_initialized[1]) {
                    FTL_TERM(1);
                    TERM_LOG_MANAGER(1);
                    std::ignore = system("rm -rf data/1");
                    device_initialized[1] = false;
                }

                // Clean up device 0 (sector)
                BaseTest::TearDown(false);
                TERM_LOG_MANAGER(g_device_index);
                TERM_SSD_CONFIG();
                device_initialized[g_device_index] = false;
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
     * Scenario: Device 0 = sector, Devices 1-2 = object
     * Verify each device can operate with its own strategy independently
     */
    TEST_P(MultiDeviceMixedTest, MixedStrategyIsolation) {
        SSDConf* ssd_config = base_test_get_ssd_config();
        size_t page_size = ssd_config->get_page_size();

        // Device 0: Sector writes
        for(size_t p = 0; p < 10; p++) {
            ASSERT_EQ(FTL_SUCCESS, FTL_WRITE_SECT(0, p * page_size, 1, NULL))
                << "Device 0 (sector) write failed at page " << p;
        }

        // Device 1: Object creates and writes
        for (unsigned long p = 1; p <= 5; p++) {
            obj_id_t object_loc = { .object_id = USEROBJECT_OID_LB + p, .partition_id = USEROBJECT_PID_LB };
            ASSERT_TRUE(FTL_OBJ_CREATE(1, object_loc, object_size_[1]))
                << "Device 1 (object) create failed";
        }

        // Device 2: Object creates and writes
        for (unsigned long p = 1; p <= 5; p++) {
            obj_id_t object_loc = { .object_id = USEROBJECT_OID_LB + p, .partition_id = USEROBJECT_PID_LB };
            ASSERT_TRUE(FTL_OBJ_CREATE(2, object_loc, object_size_[2]))
                << "Device 2 (object) create failed";
        }

        // Verify device 0 can still read
        ASSERT_EQ(FTL_SUCCESS, FTL_READ_SECT(0, 0, 1, NULL))
            << "Device 0 (sector) read failed after mixed operations";
    }

    /**
     * Test: Parallel Mixed Strategy Operations
     * Scenario: Device 0 does sector I/O while devices 1-2 do object I/O simultaneously
     */
    TEST_P(MultiDeviceMixedTest, ParallelMixedStrategyOperations) {
        SSDConf* ssd_config = base_test_get_ssd_config();
        size_t page_size = ssd_config->get_page_size();
        std::atomic<int> error_count(0);

        // Device 0: Sector worker
        auto sector_worker = [&]() {
            size_t total_pages = (ssd_config->get_pages() * 4) / 5;
            for(size_t p = 0; p < total_pages; p++) {
                if (FTL_WRITE_SECT(0, p * page_size, 1, NULL) != FTL_SUCCESS) {
                    error_count++;
                    return;
                }
            }
        };

        // Devices 1-2: Object workers
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
            std::thread t0(sector_worker);
            std::thread t1(object_worker, 1);
            std::thread t2(object_worker, 2);

            t0.join();
            t1.join();
            t2.join();

            ASSERT_EQ(0, error_count.load())
                << "Parallel mixed strategy operations failed";
        }
    }

} //namespace
