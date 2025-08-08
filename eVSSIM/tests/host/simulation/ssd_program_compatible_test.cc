/*
 * Copyright 2023 The Open University of Israel
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

#include <cmath>
#include "base_emulator_tests.h"

extern RTLogStatistics *rt_log_stats;
extern LogServer log_server;

// for performance.
#define CHECK_THRESHOLD 0.1 // 10%

using namespace std;

namespace program_compatible_test
{

    class ProgramCompatibleTest : public BaseTest
    {
    public:
        virtual void SetUp()
        {
            BaseTest::SetUp();
            INIT_LOG_MANAGER(g_device_index);
            ASSERT_EQ(_FTL_CREATE(g_device_index), FTL_SUCCESS);
        }

        virtual void TearDown()
        {
            BaseTest::TearDown();
            TERM_LOG_MANAGER(g_device_index);
            remove(GET_FILE_NAME(g_device_index));
        }
    };

    std::vector<SSDConf *> GetTestParams()
    {
        std::vector<SSDConf *> ssd_configs;
        ssd_configs.push_back(new SSDConf(pow(2, 5), 1));
        ssd_configs.push_back(new SSDConf(pow(2, 5), 2));
        return ssd_configs;
    }

    INSTANTIATE_TEST_CASE_P(DiskSize, ProgramCompatibleTest, ::testing::ValuesIn(GetTestParams()));

    TEST_P(ProgramCompatibleTest, SimpleProgramCompatibleOnOnePageTest)
    {

        SSDConf *ssd_config = base_test_get_ssd_config();
        SSDStatistics expected_stats = stats_init();

        unsigned char data[ssd_config->get_page_size() / 2];
        memset(data, 0xF0, sizeof(data));
        ASSERT_EQ(_FTL_WRITE(g_device_index, DEFAULT_NSID, 0, sizeof(data) / ssd_config->get_sector_size(), data), FTL_SUCCESS);

        unsigned char read_data[sizeof(data)];
        ASSERT_EQ(_FTL_READ(g_device_index, DEFAULT_NSID, 0, sizeof(read_data) / ssd_config->get_sector_size(), read_data), FTL_SUCCESS);
        ASSERT_EQ(memcmp(data, read_data, sizeof(data)), 0);

        expected_stats.occupied_pages++;
        MONITOR_SYNC(g_device_index, &(log_server.stats), MONITOR_SLEEP_MAX_USEC);
        ASSERT_EQ(expected_stats.occupied_pages, log_server.stats.occupied_pages);


        memset(data, 0x00, sizeof(data));
        ASSERT_EQ(_FTL_WRITE(g_device_index, DEFAULT_NSID, 0, sizeof(data) / ssd_config->get_sector_size(), data), FTL_SUCCESS);

        ASSERT_EQ(_FTL_READ(g_device_index, DEFAULT_NSID, 0, sizeof(read_data) / ssd_config->get_sector_size(), read_data), FTL_SUCCESS);
        ASSERT_EQ(memcmp(data, read_data, sizeof(data)), 0);

        MONITOR_SYNC(g_device_index, &(log_server.stats), MONITOR_SLEEP_MAX_USEC);
        ASSERT_EQ(expected_stats.occupied_pages, log_server.stats.occupied_pages);
        ASSERT_EQ(expected_stats.block_erase_count, log_server.stats.block_erase_count);

        memset(data, 0x0F, sizeof(data));
        ASSERT_EQ(_FTL_WRITE(g_device_index, DEFAULT_NSID, 0, sizeof(data) / ssd_config->get_sector_size(), data), FTL_SUCCESS);
        expected_stats.occupied_pages++;

        ASSERT_EQ(_FTL_READ(g_device_index, DEFAULT_NSID, 0, sizeof(read_data) / ssd_config->get_sector_size(), read_data), FTL_SUCCESS);
        ASSERT_EQ(memcmp(data, read_data, sizeof(data)), 0);

        MONITOR_SYNC(g_device_index, &(log_server.stats), MONITOR_SLEEP_MAX_USEC);
        ASSERT_EQ(expected_stats.occupied_pages, log_server.stats.occupied_pages);

    }

    TEST_P(ProgramCompatibleTest, SimpleProgramCompatibleOnTwoPagesTest)
    {

        SSDConf *ssd_config = base_test_get_ssd_config();
        SSDStatistics expected_stats = stats_init();

        unsigned char data[ssd_config->get_page_size() * 2];
        memset(data, 0xF0, sizeof(data));
        ASSERT_EQ(_FTL_WRITE(g_device_index, DEFAULT_NSID, 0, sizeof(data) / ssd_config->get_sector_size(), data), FTL_SUCCESS);

        unsigned char read_data[sizeof(data)];
        ASSERT_EQ(_FTL_READ(g_device_index, DEFAULT_NSID, 0, sizeof(read_data) / ssd_config->get_sector_size(), read_data), FTL_SUCCESS);
        ASSERT_EQ(memcmp(data, read_data, sizeof(data)), 0);

        expected_stats.occupied_pages+=2;
        MONITOR_SYNC(g_device_index, &(log_server.stats), MONITOR_SLEEP_MAX_USEC);
        ASSERT_EQ(expected_stats.occupied_pages, log_server.stats.occupied_pages);


        memset(data, 0x00, sizeof(data));
        ASSERT_EQ(_FTL_WRITE(g_device_index, DEFAULT_NSID, 0, sizeof(data) / ssd_config->get_sector_size(), data), FTL_SUCCESS);

        ASSERT_EQ(_FTL_READ(g_device_index, DEFAULT_NSID, 0, sizeof(read_data) / ssd_config->get_sector_size(), read_data), FTL_SUCCESS);
        ASSERT_EQ(memcmp(data, read_data, sizeof(data)), 0);

        MONITOR_SYNC(g_device_index, &(log_server.stats), MONITOR_SLEEP_MAX_USEC);
        ASSERT_EQ(expected_stats.occupied_pages, log_server.stats.occupied_pages);
        ASSERT_EQ(expected_stats.block_erase_count, log_server.stats.block_erase_count);

        memset(data, 0x0F, sizeof(data));
        ASSERT_EQ(_FTL_WRITE(g_device_index, DEFAULT_NSID, 0, sizeof(data) / ssd_config->get_sector_size(), data), FTL_SUCCESS);
        expected_stats.occupied_pages+=2;

        ASSERT_EQ(_FTL_READ(g_device_index, DEFAULT_NSID, 0, sizeof(read_data) / ssd_config->get_sector_size(), read_data), FTL_SUCCESS);
        ASSERT_EQ(memcmp(data, read_data, sizeof(data)), 0);

        MONITOR_SYNC(g_device_index, &(log_server.stats), MONITOR_SLEEP_MAX_USEC);
        ASSERT_EQ(expected_stats.occupied_pages, log_server.stats.occupied_pages);

    }

    /**
     * Like the simple case, but with more writes to the same page
     */
    TEST_P(ProgramCompatibleTest, MultipleWritesSamePageProgramCompatible)
    {

        SSDConf *ssd_config = base_test_get_ssd_config();
        SSDStatistics expected_stats = stats_init();

        unsigned char data_values[] = {
            0b11111110,
            0b11111100,
            0b11111000,
            0b11110000,
            0b11100000,
            0b11000000,
            0b10000000,
            0b00000000
        };

        unsigned char data[ssd_config->get_page_size()];
        unsigned char read_data[sizeof(data)];

        for (unsigned int i = 0; i < BASE_TEST_ARRAY_SIZE(data_values); ++i) {
            memset(data, data_values[i], sizeof(data));
            ASSERT_EQ(_FTL_WRITE(g_device_index, DEFAULT_NSID, 0, sizeof(data) / ssd_config->get_sector_size(), data), FTL_SUCCESS);

            ASSERT_EQ(_FTL_READ(g_device_index, DEFAULT_NSID, 0, sizeof(read_data) / ssd_config->get_sector_size(), read_data), FTL_SUCCESS);
            ASSERT_EQ(memcmp(data, read_data, sizeof(data)), 0);
        }

        expected_stats.occupied_pages++;
        MONITOR_SYNC(g_device_index, &(log_server.stats), MONITOR_SLEEP_MAX_USEC);
        ASSERT_EQ(expected_stats.occupied_pages, log_server.stats.occupied_pages);

    }

    /**
     * Like the simple case, but writes more than one page at once using FTL_WRITE
     */
    TEST_P(ProgramCompatibleTest, WriteProgramCompatibleMultiplePagesSingleFtlCall)
    {
        SSDConf *ssd_config = base_test_get_ssd_config();
        SSDStatistics expected_stats = stats_init();

        uint64_t offset = ssd_config->get_page_size() / 2;
        constexpr int write_page_amount = 3;
        // Because of offset, page span can be extra page
        unsigned int page_span = (offset % ssd_config->get_page_size() == 0) ? write_page_amount : write_page_amount + 1;
        unsigned char data[ssd_config->get_page_size() * write_page_amount];
        unsigned char read_data[sizeof(data)];

        memset(data, 0x0F, sizeof(data));
        ASSERT_EQ(_FTL_WRITE(g_device_index, DEFAULT_NSID, offset / ssd_config->get_sector_size(), sizeof(data) / ssd_config->get_sector_size(), data), FTL_SUCCESS);

        ASSERT_EQ(_FTL_READ(g_device_index, DEFAULT_NSID, offset / ssd_config->get_sector_size(), sizeof(read_data) / ssd_config->get_sector_size(), read_data), FTL_SUCCESS);
        ASSERT_EQ(memcmp(data, read_data, sizeof(data)), 0);

        expected_stats.occupied_pages = page_span;
        MONITOR_SYNC(g_device_index, &(log_server.stats), MONITOR_SLEEP_MAX_USEC);
        ASSERT_EQ(expected_stats.occupied_pages, log_server.stats.occupied_pages);

        // Write program compatible data
        memset(data, 0x0C, sizeof(data));
        ASSERT_EQ(_FTL_WRITE(g_device_index, DEFAULT_NSID, offset / ssd_config->get_sector_size(), sizeof(data) / ssd_config->get_sector_size(), data), FTL_SUCCESS);

        ASSERT_EQ(_FTL_READ(g_device_index, DEFAULT_NSID, offset / ssd_config->get_sector_size(), sizeof(read_data) / ssd_config->get_sector_size(), read_data), FTL_SUCCESS);
        ASSERT_EQ(memcmp(data, read_data, sizeof(data)), 0);

        MONITOR_SYNC(g_device_index, &(log_server.stats), MONITOR_SLEEP_MAX_USEC);
        ASSERT_EQ(expected_stats.occupied_pages, log_server.stats.occupied_pages);

        // Write partial compatible data - first and last pages should be compatible, while rest aren't
        // first page (partial page write)
        memset(data, 0x00, ssd_config->get_page_size() - (offset % ssd_config->get_page_size()));
        // pages 2..n-1 (full page write)
        memset(data + offset % ssd_config->get_page_size(), 0xF0, (write_page_amount - 1) * ssd_config->get_page_size());
        // last page (partial page write)
        memset(data + (write_page_amount - 1) * ssd_config->get_page_size(), 0x00, offset % ssd_config->get_page_size());
        ASSERT_EQ(_FTL_WRITE(g_device_index, DEFAULT_NSID, offset / ssd_config->get_sector_size(), sizeof(data) / ssd_config->get_sector_size(), data), FTL_SUCCESS);

        ASSERT_EQ(_FTL_READ(g_device_index, DEFAULT_NSID, offset / ssd_config->get_sector_size(), sizeof(read_data) / ssd_config->get_sector_size(), read_data), FTL_SUCCESS);
        ASSERT_EQ(memcmp(data, read_data, sizeof(data)), 0);

        expected_stats.occupied_pages += page_span - 2;
        MONITOR_SYNC(g_device_index, &(log_server.stats), MONITOR_SLEEP_MAX_USEC);
        ASSERT_EQ(expected_stats.occupied_pages, log_server.stats.occupied_pages);
    }

    /**
     * Writes to all the pages in the ssd twice but only as program compatible (1 -> 0) and then reads them,
     * checks that the stats on the monitor are correct
     */
    TEST_P(ProgramCompatibleTest, ReadAfterWriteAsProgramCompatibleToAllSDDTest)
    {
        SSDConf *ssd_config = base_test_get_ssd_config();

        uint64_t action_count = 0;
        uint64_t check_trigger = (double)ssd_config->get_pages_ns(DEFAULT_NSID) * CHECK_THRESHOLD;

        SSDStatistics expected_stats = stats_init();
        expected_stats.write_amplification = 1;

        // writes the whole ssd
        for (unsigned int p = 0; p < ssd_config->get_pages_ns(DEFAULT_NSID); p++)
        {
            unsigned char data[ssd_config->get_page_size()];
            // Write data twice to the page
            memset(data, 0x0F, sizeof(data));
            ASSERT_EQ(_FTL_WRITE(g_device_index, DEFAULT_NSID, p * ssd_config->get_page_size() / ssd_config->get_sector_size(),
            sizeof(data) / ssd_config->get_sector_size(), data), FTL_SUCCESS);
            memset(data, 0x00, sizeof(data));
            ASSERT_EQ(_FTL_WRITE(g_device_index, DEFAULT_NSID, p * ssd_config->get_page_size() / ssd_config->get_sector_size(),
            sizeof(data) / ssd_config->get_sector_size(), data), FTL_SUCCESS);

            action_count++;

            expected_stats.write_count+=2;
            expected_stats.occupied_pages++;
            expected_stats.logical_write_count+=2;


            if (action_count >= check_trigger)
            {
                action_count = 0;
                MONITOR_SYNC(g_device_index, &(log_server.stats), MONITOR_SLEEP_MAX_USEC);

                ASSERT_EQ(expected_stats.write_count, log_server.stats.write_count);
                ASSERT_EQ(expected_stats.read_count, log_server.stats.read_count);
                ASSERT_EQ(expected_stats.logical_write_count, log_server.stats.logical_write_count);
                ASSERT_EQ(expected_stats.garbage_collection_count, log_server.stats.garbage_collection_count);
                ASSERT_EQ(expected_stats.write_amplification, log_server.stats.write_amplification);
                ASSERT_EQ(expected_stats.occupied_pages, log_server.stats.occupied_pages);
                ASSERT_EQ(expected_stats.block_erase_count, log_server.stats.block_erase_count);
            }
        }


        // reads the whole ssd
        for (unsigned int p = 0; p < ssd_config->get_pages_ns(DEFAULT_NSID); p++)
        {
            unsigned char data[ssd_config->get_page_size()];
            memset(data, 0x00, sizeof(data));
            unsigned char read_data[ssd_config->get_page_size()];

            ASSERT_EQ(_FTL_READ(g_device_index, DEFAULT_NSID, p * ssd_config->get_page_size() / ssd_config->get_sector_size(),
            sizeof(read_data) / ssd_config->get_sector_size(), read_data), FTL_SUCCESS);
            ASSERT_EQ(memcmp(data, read_data, sizeof(data)), 0);

            action_count++;

            expected_stats.read_count++;


            if (action_count >= check_trigger)
            {
                action_count = 0;
                MONITOR_SYNC(g_device_index, &(log_server.stats), MONITOR_SLEEP_MAX_USEC);

                ASSERT_EQ(expected_stats.write_count, log_server.stats.write_count);
                ASSERT_EQ(expected_stats.read_count, log_server.stats.read_count);
                ASSERT_EQ(expected_stats.logical_write_count, log_server.stats.logical_write_count);
                ASSERT_EQ(expected_stats.garbage_collection_count, log_server.stats.garbage_collection_count);
                ASSERT_EQ(expected_stats.write_amplification, log_server.stats.write_amplification);
                ASSERT_EQ(expected_stats.occupied_pages, log_server.stats.occupied_pages);
                ASSERT_EQ(expected_stats.block_erase_count, log_server.stats.block_erase_count);
            }
        }

        MONITOR_SYNC(g_device_index, &(log_server.stats), MONITOR_SLEEP_MAX_USEC);

        // checks that log_server.stats (the stats on the monitor) are accurate
        ASSERT_EQ(ssd_config->get_pages_ns(DEFAULT_NSID), log_server.stats.read_count);
        ASSERT_EQ(2 * ssd_config->get_pages_ns(DEFAULT_NSID), log_server.stats.write_count); // We write twice to every page but as program compatible (only 1 -> 0)
        ASSERT_EQ(1, log_server.stats.write_amplification);
        ASSERT_EQ(0, log_server.stats.garbage_collection_count); // Because the write we do is program compatible GC shouldn't be called
    }

    /**
     * Writes to all the pages in the ssd twice but not as program compatible (0 -> 1) and then reads them,
     * checks that the stats on the monitor are correct
     */
    TEST_P(ProgramCompatibleTest, ReadAfterWriteNotAsProgramCompatibleToAllSDDTest)
    {
        SSDConf *ssd_config = base_test_get_ssd_config();

        uint64_t action_count = 0;
        uint64_t check_trigger = (double)ssd_config->get_pages_ns(DEFAULT_NSID) * CHECK_THRESHOLD;

        SSDStatistics expected_stats = stats_init();
        expected_stats.write_amplification = 1;

        // writes the whole ssd with 0s
        for (unsigned int p = 0; p < ssd_config->get_pages_ns(DEFAULT_NSID); p++)
        {
            unsigned char data[ssd_config->get_page_size()];
            memset(data, 0x00, sizeof(data));
            ASSERT_EQ(_FTL_WRITE(g_device_index, DEFAULT_NSID, p * ssd_config->get_page_size() / ssd_config->get_sector_size(),
            sizeof(data) / ssd_config->get_sector_size(), data), FTL_SUCCESS);

            action_count++;

            expected_stats.write_count++;
            expected_stats.occupied_pages++;
            expected_stats.logical_write_count++;


            if (action_count >= check_trigger)
            {
                action_count = 0;
                MONITOR_SYNC(g_device_index, &(log_server.stats), MONITOR_SLEEP_MAX_USEC);

                ASSERT_EQ(expected_stats.write_count, log_server.stats.write_count);
                ASSERT_EQ(expected_stats.read_count, log_server.stats.read_count);
                ASSERT_EQ(expected_stats.logical_write_count, log_server.stats.logical_write_count);
                ASSERT_EQ(expected_stats.garbage_collection_count, log_server.stats.garbage_collection_count);
                ASSERT_EQ(expected_stats.write_amplification, log_server.stats.write_amplification);
                ASSERT_EQ(expected_stats.occupied_pages, log_server.stats.occupied_pages);
                ASSERT_EQ(expected_stats.block_erase_count, log_server.stats.block_erase_count);
            }
        }

        // writes the whole ssd with data with 1s
        for (unsigned int p = 0; p < ssd_config->get_pages_ns(DEFAULT_NSID); p++)
        {
            unsigned char data[ssd_config->get_page_size()];
            memset(data, 0xF0, sizeof(data));
            ASSERT_EQ(_FTL_WRITE(g_device_index, DEFAULT_NSID, p * ssd_config->get_page_size() / ssd_config->get_sector_size(),
            sizeof(data) / ssd_config->get_sector_size(), data), FTL_SUCCESS);

            action_count++;

            expected_stats.write_count++;
            expected_stats.occupied_pages++;
            expected_stats.logical_write_count++;
            expected_stats.block_erase_count++;


            if (action_count >= check_trigger)
            {
                action_count = 0;
                MONITOR_SYNC(g_device_index, &(log_server.stats), MONITOR_SLEEP_MAX_USEC);

                ASSERT_LE(expected_stats.write_count, log_server.stats.write_count);

                // The read should be the number of not asked writes (because read happens for copying)
                expected_stats.read_count = log_server.stats.write_count - expected_stats.write_count;
                ASSERT_EQ(expected_stats.read_count, log_server.stats.read_count);
                ASSERT_EQ(expected_stats.logical_write_count, log_server.stats.logical_write_count);
                ASSERT_LT(0, log_server.stats.garbage_collection_count);

                // TODO liel - check and understand
                // ASSERT_EQ(expected_stats.occupied_pages, log_server.stats.occupied_pages);
                ASSERT_EQ(log_server.stats.garbage_collection_count, log_server.stats.block_erase_count);
            }
        }


        // The read should be the number of not asked writes (because read happens for copying)
        MONITOR_SYNC(g_device_index, &(log_server.stats), MONITOR_SLEEP_MAX_USEC);
        expected_stats.read_count = log_server.stats.write_count - expected_stats.write_count;

        // reads the whole ssd
        for (unsigned int p = 0; p < ssd_config->get_pages_ns(DEFAULT_NSID); p++)
        {
            unsigned char data[ssd_config->get_page_size()];
            memset(data, 0xF0, sizeof(data));
            unsigned char read_data[ssd_config->get_page_size()];
            ASSERT_EQ(_FTL_READ(g_device_index, DEFAULT_NSID, p * ssd_config->get_page_size() / ssd_config->get_sector_size(),
            sizeof(read_data) / ssd_config->get_sector_size(), read_data), FTL_SUCCESS);
            // This check that copyback happens as expected during GC.
            ASSERT_EQ(memcmp(data, read_data, sizeof(data)), 0);

            action_count++;

            expected_stats.read_count++;


            if (action_count >= check_trigger)
            {
                action_count = 0;
                MONITOR_SYNC(g_device_index, &(log_server.stats), MONITOR_SLEEP_MAX_USEC);

                ASSERT_LE(expected_stats.write_count, log_server.stats.write_count);
                ASSERT_EQ(expected_stats.read_count, log_server.stats.read_count);
                ASSERT_EQ(expected_stats.logical_write_count, log_server.stats.logical_write_count);
                ASSERT_EQ(log_server.stats.garbage_collection_count, log_server.stats.block_erase_count);
            }
        }

        MONITOR_SYNC(g_device_index, &(log_server.stats), MONITOR_SLEEP_MAX_USEC);

        // checks that log_server.stats (the stats on the monitor) are accurate
        ASSERT_LT(2 * ssd_config->get_pages_ns(DEFAULT_NSID), log_server.stats.write_count); // We write twice to every page but not as program compatible.
        ASSERT_LT(0, log_server.stats.garbage_collection_count); // Because the write we do is NOT program compatible GC should be called
    }
} // namespace
