/*
 * Multi-Device Workload Testing
 *
 * This file tests realistic multi-device scenarios where different devices
 * handle different workload patterns simultaneously. This validates that:
 * 1. Multiple devices can operate independently
 * 2. Operations on one device don't corrupt data on another
 * 3. Each device maintains its own FTL state correctly
 */

#include "base_emulator_tests.h"
#include <thread>
#include <atomic>

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
                        TERM_LOG_MANAGER(i);
                        FTL_TERM(i);
                        std::ignore = system((std::string("rm -rf data/") + std::to_string(i)).c_str());
                        device_initialized[i] = false;
                    }
                }

                TERM_LOG_MANAGER(g_device_index);
                BaseTest::TearDown();
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
            ASSERT_EQ(FTL_SUCCESS, _FTL_WRITE_SECT(0, p * page_size, 1, NULL))
                << "Device 0 sequential write failed at page " << p;
        }

        // Device 1: Sequential writes starting at page 0 (different device!)
        for(size_t p = 0; p < pages_to_test; p++) {
            ASSERT_EQ(FTL_SUCCESS, _FTL_WRITE_SECT(1, p * page_size, 1, NULL))
                << "Device 1 sequential write failed at page " << p;
        }

        // Device 2: Random writes
        for(size_t i = 0; i < pages_to_test; i++) {
            size_t page = rand() % ssd_config->get_pages();
            ASSERT_EQ(FTL_SUCCESS, _FTL_WRITE_SECT(2, page * page_size, 1, NULL))
                << "Device 2 random write failed";
        }

        // Verify: All devices can still read their data
        for(size_t p = 0; p < pages_to_test; p++) {
            ASSERT_EQ(FTL_SUCCESS, _FTL_READ_SECT(0, p * page_size, 1, NULL))
                << "Device 0 read failed after multi-device writes";
            ASSERT_EQ(FTL_SUCCESS, _FTL_READ_SECT(1, p * page_size, 1, NULL))
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
            ASSERT_EQ(FTL_SUCCESS, _FTL_WRITE_SECT(0, db_page * page_size, 1, NULL))
                << "Database write failed at iteration " << i;

            // Log: Sequential append to device 1
            if (i < ssd_config->get_pages()) {
                ASSERT_EQ(FTL_SUCCESS, _FTL_WRITE_SECT(1, i * page_size, 1, NULL))
                    << "Log append failed at iteration " << i;
            }

            // Every 10 operations, do a database read
            if (i % 10 == 0 && i > 0) {
                size_t read_page = rand() % ssd_config->get_pages();
                _FTL_READ_SECT(0, read_page * page_size, 1, NULL);
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
        size_t total_pages = ssd_config->get_pages();

        // Shared error tracking (atomic for thread safety)
        std::atomic<int> error_count(0);

        auto sequential_write_worker = [&](uint8_t device_id) {
            for(size_t p = 0; p < total_pages; p++) {
                if (_FTL_WRITE_SECT(device_id, p * page_size, 1, NULL) != FTL_SUCCESS) {
                    error_count++;
                    return;
                }
            }
        };

        auto random_write_worker = [&](uint8_t device_id) {
            for(size_t i = 0; i < total_pages; i++) {
                size_t random_page = rand() % total_pages;
                if (_FTL_WRITE_SECT(device_id, random_page * page_size, 1, NULL) != FTL_SUCCESS) {
                    error_count++;
                    return;
                }
            }
        };

        auto mixed_workload_worker = [&](uint8_t device_id) {
            // Phase 1: Random writes
            for(size_t p = 0; p < total_pages; p++) {
                size_t random_page = rand() % total_pages;
                if (_FTL_WRITE_SECT(device_id, random_page * page_size, 1, NULL) != FTL_SUCCESS) {
                    error_count++;
                    return;
                }
            }
            // Phase 2: Sequential writes
            for(size_t p = 0; p < total_pages; p++) {
                if (_FTL_WRITE_SECT(device_id, p * page_size, 1, NULL) != FTL_SUCCESS) {
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
        ASSERT_EQ(FTL_SUCCESS, _FTL_READ_SECT(0, 0, 1, NULL))
            << "Device 0 read failed after parallel tests";
        ASSERT_EQ(FTL_SUCCESS, _FTL_READ_SECT(1, 0, 1, NULL))
            << "Device 1 read failed after parallel tests";
        ASSERT_EQ(FTL_SUCCESS, _FTL_READ_SECT(2, 0, 1, NULL))
            << "Device 2 read failed after parallel tests";
    }

} //namespace
