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
#include <thread>
#include "base_emulator_tests.h"

extern RTLogStatistics* rt_log_stats;
extern LogServer log_server;

// 2^10 mb = 1 GB
// 2^22 nb = 4 TB
#define MAX_POW 10

// for performance.
#define CHECK_THRESHOLD 0.1 // 10%

// WRITEREADTest already covers this assertion block.
#define WRITEREADWRITETest_ASSERT_ANYWAY false

using namespace std;

namespace write_read_test
{

    class WriteReadTest : public BaseTest
    {
    public:
        virtual void SetUp()
        {
            BaseTest::SetUp();
            ASSERT_EQ(_FTL_CREATE(g_device_index), FTL_SUCCESS);
            INIT_LOG_MANAGER(g_device_index);
        }

        virtual void TearDown()
        {
            BaseTest::TearDown(false);
            TERM_LOG_MANAGER(g_device_index);
            TERM_SSD_CONFIG();
        }
    };

    std::vector<SSDConf *> GetTestParams()
    {
        std::vector<SSDConf *> ssd_configs;

        for (unsigned int i = 1; i <= MAX_POW; i++)
        {
            ssd_configs.push_back(new SSDConf(pow(2, i)));
        }

        return ssd_configs;
    }

    /**
     * this test fills the entire disk in iterations starting from a disk size of 2 pages and continues until 2 ^ MAX_POW
     * the test never writes on a used page so that it wont incure garbage collection and keep the test deterministic
     * this test has 3 cases one in which it only writes, writes then reads and writes the entire disk then reads it
     */

    INSTANTIATE_TEST_CASE_P(DiskSize, WriteReadTest, ::testing::ValuesIn(GetTestParams()));

    /**
     * writes 2^n pages to the ssd in the default namespace,
     * checks that the stats on the monitor are correct
     */
    TEST_P(WriteReadTest, WRITEOnlyTest)
    {
        SSDConf *ssd_config = base_test_get_ssd_config();

        uint64_t action_count = 0;
        uint64_t check_trigger = (double)ssd_config->get_pages_ns(ID_SECTOR_NS0) + (double)ssd_config->get_pages_ns(ID_SECTOR_NS1) * CHECK_THRESHOLD;

        SSDStatistics expected_stats = stats_init();
        expected_stats.write_amplification = 1;
        uint64_t prev_channele = -1;

        for (size_t p = 0; p < ssd_config->get_pages_ns(ID_SECTOR_NS0); p++)
        {
            FTL_WRITE_SECT(g_device_index, ID_SECTOR_NS0, p * ssd_config->get_page_size(), 1, NULL);

            action_count++;

            expected_stats.write_count++;
            expected_stats.occupied_pages++;
            expected_stats.logical_write_count++;

            expected_stats.write_elapsed_time += devices[g_device_index].reg_write_delay + devices[g_device_index].cell_program_delay;
            uint64_t cur_channele = CALC_FLASH(g_device_index, GET_MAPPING_INFO(g_device_index, ID_SECTOR_NS0, p)) % devices[g_device_index].channel_nb;

            if (cur_channele != prev_channele)
            {
                expected_stats.write_elapsed_time += devices[g_device_index].channel_switch_delay_w;
                expected_stats.channel_switch_to_write++;
            }
            prev_channele = cur_channele;

            expected_stats.utilization = (double)expected_stats.occupied_pages / devices[g_device_index].pages_in_ssd;
        }

        for (size_t p = 0; p < ssd_config->get_pages_ns(ID_SECTOR_NS1); p++)
        {
            FTL_WRITE_SECT(g_device_index, ID_SECTOR_NS1, p * ssd_config->get_page_size(), 1, NULL);

            action_count++;

            expected_stats.write_count++;
            expected_stats.occupied_pages++;
            expected_stats.logical_write_count++;

            expected_stats.write_elapsed_time += devices[g_device_index].reg_write_delay + devices[g_device_index].cell_program_delay;
            uint64_t cur_channele = CALC_FLASH(g_device_index, GET_MAPPING_INFO(g_device_index, ID_SECTOR_NS1, p)) % devices[g_device_index].channel_nb;

            if (cur_channele != prev_channele)
            {
                expected_stats.write_elapsed_time += devices[g_device_index].channel_switch_delay_w;
                expected_stats.channel_switch_to_write++;
            }
            prev_channele = cur_channele;

            expected_stats.utilization = (double)expected_stats.occupied_pages / devices[g_device_index].pages_in_ssd;

            if (action_count >= check_trigger)
            {
                action_count = 0;
                MONITOR_SYNC(g_device_index, &(log_server.stats), MONITOR_SLEEP_MAX_USEC);

                ASSERT_EQ(expected_stats.write_count, log_server.stats.write_count);
                ASSERT_EQ(expected_stats.read_count, log_server.stats.read_count);

                ASSERT_EQ(expected_stats.write_elapsed_time, log_server.stats.write_elapsed_time);
                ASSERT_EQ(expected_stats.read_elapsed_time, log_server.stats.read_elapsed_time);

                ASSERT_EQ(expected_stats.logical_write_count, log_server.stats.logical_write_count);
                ASSERT_EQ(expected_stats.garbage_collection_count, log_server.stats.garbage_collection_count);
                ASSERT_EQ(expected_stats.write_amplification, log_server.stats.write_amplification);
                ASSERT_EQ(expected_stats.utilization, log_server.stats.utilization);

                ASSERT_EQ(expected_stats.occupied_pages, log_server.stats.occupied_pages);

                ASSERT_EQ(expected_stats.block_erase_count, log_server.stats.block_erase_count);
                ASSERT_EQ(expected_stats.channel_switch_to_write, log_server.stats.channel_switch_to_write);
                ASSERT_EQ(expected_stats.channel_switch_to_read, log_server.stats.channel_switch_to_read);
            }
        }

        MONITOR_SYNC(g_device_index, &(log_server.stats), MONITOR_SLEEP_MAX_USEC);

        unsigned int time_per_action = devices[g_device_index].reg_write_delay + devices[g_device_index].cell_program_delay + devices[g_device_index].channel_switch_delay_w;
        double write_speed = CALCULATEMBPS(ssd_config->get_page_size(), time_per_action);

        // checks that log_server.stats (the stats on the monitor) are accurate
        ASSERT_NEAR(write_speed, log_server.stats.write_speed, ERROR_THRESHHOLD(write_speed));
        ASSERT_EQ(0, log_server.stats.read_speed);
        ASSERT_EQ(0, log_server.stats.read_count);
        ASSERT_EQ(ssd_config->get_pages_ns(ID_SECTOR_NS0) + ssd_config->get_pages_ns(ID_SECTOR_NS1), log_server.stats.write_count);
        ASSERT_EQ(1, log_server.stats.write_amplification);
        ASSERT_EQ(0.8, log_server.stats.utilization);
        ASSERT_EQ(0, log_server.stats.garbage_collection_count);
    }

    /**
     * writes 2^n pages to the ssd and then reads them,
     * checks that the stats on the monitor are correct
     */
    TEST_P(WriteReadTest, ReadAfterWriteTest)
    {
        SSDConf *ssd_config = base_test_get_ssd_config();

        uint64_t action_count = 0;
        uint64_t check_trigger = (double)ssd_config->get_pages_ns(ID_SECTOR_NS0) + (double)ssd_config->get_pages_ns(ID_SECTOR_NS1) * CHECK_THRESHOLD;

        SSDStatistics expected_stats = stats_init();
        expected_stats.write_amplification = 1;
        uint64_t prev_channele = -1;

        for (unsigned int p = 0; p < ssd_config->get_pages_ns(ID_SECTOR_NS0); p++)
        {
            FTL_WRITE_SECT(g_device_index, ID_SECTOR_NS0, p * ssd_config->get_page_size(), 1, NULL);

            action_count++;

            expected_stats.write_count++;
            expected_stats.occupied_pages++;
            expected_stats.logical_write_count++;

            expected_stats.write_elapsed_time += devices[g_device_index].reg_write_delay + devices[g_device_index].cell_program_delay;

            uint64_t cur_channele = CALC_FLASH(g_device_index, GET_MAPPING_INFO(g_device_index, ID_SECTOR_NS0, p)) % devices[g_device_index].channel_nb;

            if (cur_channele != prev_channele)
            {
                expected_stats.write_elapsed_time += devices[g_device_index].channel_switch_delay_w;
                expected_stats.channel_switch_to_write++;
            }
            prev_channele = cur_channele;

            expected_stats.utilization = (double)expected_stats.occupied_pages / devices[g_device_index].pages_in_ssd;
        }

        for (unsigned int p = 0; p < ssd_config->get_pages_ns(ID_SECTOR_NS1); p++)
        {
            FTL_WRITE_SECT(g_device_index, ID_SECTOR_NS1, p * ssd_config->get_page_size(), 1, NULL);

            action_count++;

            expected_stats.write_count++;
            expected_stats.occupied_pages++;
            expected_stats.logical_write_count++;

            expected_stats.write_elapsed_time += devices[g_device_index].reg_write_delay + devices[g_device_index].cell_program_delay;

            uint64_t cur_channele = CALC_FLASH(g_device_index, GET_MAPPING_INFO(g_device_index, ID_SECTOR_NS1, p)) % devices[g_device_index].channel_nb;

            if (cur_channele != prev_channele)
            {
                expected_stats.write_elapsed_time += devices[g_device_index].channel_switch_delay_w;
                expected_stats.channel_switch_to_write++;
            }
            prev_channele = cur_channele;

            expected_stats.utilization = (double)expected_stats.occupied_pages / devices[g_device_index].pages_in_ssd;

            if (action_count >= check_trigger)
            {
                action_count = 0;
                MONITOR_SYNC(g_device_index, &(log_server.stats), MONITOR_SLEEP_MAX_USEC);

                ASSERT_EQ(expected_stats.write_count, log_server.stats.write_count);
                ASSERT_EQ(expected_stats.read_count, log_server.stats.read_count);

                ASSERT_EQ(expected_stats.write_elapsed_time, log_server.stats.write_elapsed_time);
                ASSERT_EQ(expected_stats.read_elapsed_time, log_server.stats.read_elapsed_time);

                ASSERT_EQ(expected_stats.logical_write_count, log_server.stats.logical_write_count);
                ASSERT_EQ(expected_stats.garbage_collection_count, log_server.stats.garbage_collection_count);
                ASSERT_EQ(expected_stats.write_amplification, log_server.stats.write_amplification);
                ASSERT_EQ(expected_stats.utilization, log_server.stats.utilization);

                ASSERT_EQ(expected_stats.occupied_pages, log_server.stats.occupied_pages);

                ASSERT_EQ(expected_stats.block_erase_count, log_server.stats.block_erase_count);
                ASSERT_EQ(expected_stats.channel_switch_to_write, log_server.stats.channel_switch_to_write);
                ASSERT_EQ(expected_stats.channel_switch_to_read, log_server.stats.channel_switch_to_read);
            }
        }

        unsigned int time_per_write = devices[g_device_index].reg_write_delay + devices[g_device_index].cell_program_delay + devices[g_device_index].channel_switch_delay_w;

        prev_channele = -1;

        for (unsigned int p = 0; p < ssd_config->get_pages_ns(ID_SECTOR_NS0); p++)
        {
            FTL_READ_SECT(g_device_index, ID_SECTOR_NS0, p * ssd_config->get_page_size(), 1, NULL);

            action_count++;

            expected_stats.read_count++;

            expected_stats.read_elapsed_time += devices[g_device_index].reg_read_delay + devices[g_device_index].cell_read_delay;

            uint64_t cur_channele = CALC_FLASH(g_device_index, GET_MAPPING_INFO(g_device_index, ID_SECTOR_NS0, p)) % devices[g_device_index].channel_nb;

            if (cur_channele != prev_channele)
            {
                expected_stats.read_elapsed_time += devices[g_device_index].channel_switch_delay_r;
                expected_stats.channel_switch_to_read++;
            }

            prev_channele = cur_channele;
        }

        for (unsigned int p = 0; p < ssd_config->get_pages_ns(ID_SECTOR_NS1); p++)
        {
            FTL_READ_SECT(g_device_index, ID_SECTOR_NS1, p * ssd_config->get_page_size(), 1, NULL);

            action_count++;

            expected_stats.read_count++;

            expected_stats.read_elapsed_time += devices[g_device_index].reg_read_delay + devices[g_device_index].cell_read_delay;

            uint64_t cur_channele = CALC_FLASH(g_device_index, GET_MAPPING_INFO(g_device_index, ID_SECTOR_NS1, p)) % devices[g_device_index].channel_nb;

            if (cur_channele != prev_channele)
            {
                expected_stats.read_elapsed_time += devices[g_device_index].channel_switch_delay_r;
                expected_stats.channel_switch_to_read++;
            }

            prev_channele = cur_channele;

            if (action_count >= check_trigger)
            {
                action_count = 0;
                MONITOR_SYNC(g_device_index, &(log_server.stats), MONITOR_SLEEP_MAX_USEC);

                ASSERT_EQ(expected_stats.write_count, log_server.stats.write_count);
                ASSERT_EQ(expected_stats.read_count, log_server.stats.read_count);

                ASSERT_EQ(expected_stats.write_elapsed_time, log_server.stats.write_elapsed_time);
                ASSERT_EQ(expected_stats.read_elapsed_time, log_server.stats.read_elapsed_time);

                ASSERT_EQ(expected_stats.logical_write_count, log_server.stats.logical_write_count);
                ASSERT_EQ(expected_stats.garbage_collection_count, log_server.stats.garbage_collection_count);
                ASSERT_EQ(expected_stats.write_amplification, log_server.stats.write_amplification);
                ASSERT_EQ(expected_stats.utilization, log_server.stats.utilization);

                ASSERT_EQ(expected_stats.occupied_pages, log_server.stats.occupied_pages);

                ASSERT_EQ(expected_stats.block_erase_count, log_server.stats.block_erase_count);
                ASSERT_EQ(expected_stats.channel_switch_to_write, log_server.stats.channel_switch_to_write);
                ASSERT_EQ(expected_stats.channel_switch_to_read, log_server.stats.channel_switch_to_read);
            }
        }

        unsigned int time_per_read = devices[g_device_index].reg_read_delay + devices[g_device_index].cell_read_delay + devices[g_device_index].channel_switch_delay_r;

        MONITOR_SYNC(g_device_index, &(log_server.stats), MONITOR_SLEEP_MAX_USEC);

        double write_speed = CALCULATEMBPS(ssd_config->get_page_size(), time_per_write);

        double read_speed = CALCULATEMBPS(ssd_config->get_page_size(), time_per_read);

        // checks that log_server.stats (the stats on the monitor) are accurate
        ASSERT_NEAR(write_speed, log_server.stats.write_speed, ERROR_THRESHHOLD(write_speed));
        ASSERT_NEAR(read_speed, log_server.stats.read_speed, ERROR_THRESHHOLD(read_speed));
        ASSERT_EQ(ssd_config->get_pages_ns(ID_SECTOR_NS0) + ssd_config->get_pages_ns(ID_SECTOR_NS1), log_server.stats.read_count);
        ASSERT_EQ(ssd_config->get_pages_ns(ID_SECTOR_NS0) + ssd_config->get_pages_ns(ID_SECTOR_NS1), log_server.stats.write_count);
        ASSERT_EQ(1, log_server.stats.write_amplification);
        ASSERT_EQ(0.8, log_server.stats.utilization);
        ASSERT_EQ(0, log_server.stats.garbage_collection_count);
    }

    /**
     * writes and then reads one page on the ssd 2^n times,
     * checks that the stats on the monitor are correct
     */
    TEST_P(WriteReadTest, WRITEREADTest)
    {
        SSDConf *ssd_config = base_test_get_ssd_config();

        uint64_t action_count = 0;
        uint64_t check_trigger = (double)ssd_config->get_pages_ns(ID_SECTOR_NS0) + (double)ssd_config->get_pages_ns(ID_SECTOR_NS1) * CHECK_THRESHOLD;

        SSDStatistics expected_stats = stats_init();
        expected_stats.write_amplification = 1;

        // writes and reads pages one at a time

        for (unsigned int p = 0; p < ssd_config->get_pages_ns(ID_SECTOR_NS0); p++)
        {
            FTL_WRITE_SECT(g_device_index, ID_SECTOR_NS0, p * ssd_config->get_page_size(), 1, NULL);
            FTL_READ_SECT(g_device_index, ID_SECTOR_NS0, p * ssd_config->get_page_size(), 1, NULL);

            action_count++;

            expected_stats.write_count++;
            expected_stats.occupied_pages++;
            expected_stats.logical_write_count++;

            expected_stats.write_elapsed_time += devices[g_device_index].reg_write_delay + devices[g_device_index].cell_program_delay;
            expected_stats.write_elapsed_time += devices[g_device_index].channel_switch_delay_w;
            expected_stats.channel_switch_to_write++;

            expected_stats.read_count++;
            expected_stats.read_elapsed_time += devices[g_device_index].reg_read_delay + devices[g_device_index].cell_read_delay;
            expected_stats.read_elapsed_time += devices[g_device_index].channel_switch_delay_r;
            expected_stats.channel_switch_to_read++;

            expected_stats.utilization = (double)expected_stats.occupied_pages / devices[g_device_index].pages_in_ssd;
        }

        for (unsigned int p = 0; p < ssd_config->get_pages_ns(ID_SECTOR_NS1); p++)
        {
            FTL_WRITE_SECT(g_device_index, ID_SECTOR_NS1, p * ssd_config->get_page_size(), 1, NULL);
            FTL_READ_SECT(g_device_index, ID_SECTOR_NS1, p * ssd_config->get_page_size(), 1, NULL);

            action_count++;

            expected_stats.write_count++;
            expected_stats.occupied_pages++;
            expected_stats.logical_write_count++;

            expected_stats.write_elapsed_time += devices[g_device_index].reg_write_delay + devices[g_device_index].cell_program_delay;
            expected_stats.write_elapsed_time += devices[g_device_index].channel_switch_delay_w;
            expected_stats.channel_switch_to_write++;

            expected_stats.read_count++;
            expected_stats.read_elapsed_time += devices[g_device_index].reg_read_delay + devices[g_device_index].cell_read_delay;
            expected_stats.read_elapsed_time += devices[g_device_index].channel_switch_delay_r;
            expected_stats.channel_switch_to_read++;

            expected_stats.utilization = (double)expected_stats.occupied_pages / devices[g_device_index].pages_in_ssd;

            if (action_count >= check_trigger)
            {
                action_count = 0;
                MONITOR_SYNC(g_device_index, &(log_server.stats), MONITOR_SLEEP_MAX_USEC);

                ASSERT_EQ(expected_stats.write_count, log_server.stats.write_count);
                ASSERT_EQ(expected_stats.read_count, log_server.stats.read_count);

                ASSERT_EQ(expected_stats.write_elapsed_time, log_server.stats.write_elapsed_time);
                ASSERT_EQ(expected_stats.read_elapsed_time, log_server.stats.read_elapsed_time);

                ASSERT_EQ(expected_stats.logical_write_count, log_server.stats.logical_write_count);
                ASSERT_EQ(expected_stats.garbage_collection_count, log_server.stats.garbage_collection_count);
                ASSERT_EQ(expected_stats.write_amplification, log_server.stats.write_amplification);
                ASSERT_EQ(expected_stats.utilization, log_server.stats.utilization);

                ASSERT_EQ(expected_stats.occupied_pages, log_server.stats.occupied_pages);

                ASSERT_EQ(expected_stats.block_erase_count, log_server.stats.block_erase_count);
                ASSERT_EQ(expected_stats.channel_switch_to_write, log_server.stats.channel_switch_to_write);
                ASSERT_EQ(expected_stats.channel_switch_to_read, log_server.stats.channel_switch_to_read);
            }
        }

        // uint64_t total_time_write = (devices[g_device_index].reg_write_delay + devices[g_device_index].cell_program_delay) * ssd_config->get_pages() + devices[g_device_index].channel_switch_delay_w * (ssd_config->get_channel_nb());
        // double write_speed = CALCULATEMBPS(ssd_config->get_page_size() * ssd_config->get_page_nb(), total_time);

        unsigned int time_per_write = devices[g_device_index].reg_write_delay + devices[g_device_index].cell_program_delay + devices[g_device_index].channel_switch_delay_w;
        unsigned int time_per_read = devices[g_device_index].reg_read_delay + devices[g_device_index].cell_read_delay + devices[g_device_index].channel_switch_delay_r;

        MONITOR_SYNC(g_device_index, &(log_server.stats), MONITOR_SLEEP_MAX_USEC);

        double write_speed = CALCULATEMBPS(ssd_config->get_page_size(), time_per_write);

        double read_speed = CALCULATEMBPS(ssd_config->get_page_size(), time_per_read);

        // checks that log_server.stats (the stats on the monitor) are accurate
        ASSERT_NEAR(write_speed, log_server.stats.write_speed, ERROR_THRESHHOLD(write_speed));
        ASSERT_NEAR(read_speed, log_server.stats.read_speed, ERROR_THRESHHOLD(read_speed));
        ASSERT_EQ(ssd_config->get_pages_ns(ID_SECTOR_NS0) + ssd_config->get_pages_ns(ID_SECTOR_NS1), log_server.stats.read_count);
        ASSERT_EQ(ssd_config->get_pages_ns(ID_SECTOR_NS0) + ssd_config->get_pages_ns(ID_SECTOR_NS1), log_server.stats.write_count);
        ASSERT_EQ(1, log_server.stats.write_amplification);
        ASSERT_EQ(0.8, log_server.stats.utilization);
        ASSERT_EQ(0, log_server.stats.garbage_collection_count);
    }

    /**
     * writes and then reads one page on the ssd 2^n times,
     * after that write again to trigger GC
     * checks that the stats on the monitor are correct
     */
    TEST_P(WriteReadTest, WRITEREADWRITETest)
    {
        SSDConf *ssd_config = base_test_get_ssd_config();

        uint64_t action_count = 0;
        uint64_t check_trigger = (double)ssd_config->get_pages_ns(ID_SECTOR_NS0) + (double)ssd_config->get_pages_ns(ID_SECTOR_NS1) * CHECK_THRESHOLD;

        SSDStatistics expected_stats = stats_init();
        expected_stats.write_amplification = 1;

        for (unsigned int p = 0; p < ssd_config->get_pages_ns(ID_SECTOR_NS0); p++)
        {
            FTL_WRITE_SECT(g_device_index, ID_SECTOR_NS0, p * ssd_config->get_page_size(), 1, NULL);
            FTL_READ_SECT(g_device_index, ID_SECTOR_NS0, p * ssd_config->get_page_size(), 1, NULL);

            action_count++;

            expected_stats.write_count++;
            expected_stats.occupied_pages++;
            expected_stats.logical_write_count++;

            expected_stats.write_elapsed_time += devices[g_device_index].reg_write_delay + devices[g_device_index].cell_program_delay;
            expected_stats.write_elapsed_time += devices[g_device_index].channel_switch_delay_w;
            expected_stats.channel_switch_to_write++;

            expected_stats.read_count++;
            expected_stats.read_elapsed_time += devices[g_device_index].reg_read_delay + devices[g_device_index].cell_read_delay;
            expected_stats.read_elapsed_time += devices[g_device_index].channel_switch_delay_r;
            expected_stats.channel_switch_to_read++;

            expected_stats.utilization = (double)expected_stats.occupied_pages / devices[g_device_index].pages_in_ssd;
        }

        for (unsigned int p = 0; p < ssd_config->get_pages_ns(ID_SECTOR_NS1); p++)
        {
            FTL_WRITE_SECT(g_device_index, ID_SECTOR_NS1, p * ssd_config->get_page_size(), 1, NULL);
            FTL_READ_SECT(g_device_index, ID_SECTOR_NS1, p * ssd_config->get_page_size(), 1, NULL);

            action_count++;

            expected_stats.write_count++;
            expected_stats.occupied_pages++;
            expected_stats.logical_write_count++;

            expected_stats.write_elapsed_time += devices[g_device_index].reg_write_delay + devices[g_device_index].cell_program_delay;
            expected_stats.write_elapsed_time += devices[g_device_index].channel_switch_delay_w;
            expected_stats.channel_switch_to_write++;

            expected_stats.read_count++;
            expected_stats.read_elapsed_time += devices[g_device_index].reg_read_delay + devices[g_device_index].cell_read_delay;
            expected_stats.read_elapsed_time += devices[g_device_index].channel_switch_delay_r;
            expected_stats.channel_switch_to_read++;

            expected_stats.utilization = (double)expected_stats.occupied_pages / devices[g_device_index].pages_in_ssd;

            // WRITEREADTest already covers this assertion block.
            if (WRITEREADWRITETest_ASSERT_ANYWAY && action_count >= check_trigger)
            {
                action_count = 0;
                MONITOR_SYNC(g_device_index, &(log_server.stats), MONITOR_SLEEP_MAX_USEC);

                EXPECT_EQ(expected_stats.write_count, log_server.stats.write_count);
                EXPECT_EQ(expected_stats.read_count, log_server.stats.read_count);

                EXPECT_EQ(expected_stats.write_elapsed_time, log_server.stats.write_elapsed_time);
                EXPECT_EQ(expected_stats.read_elapsed_time, log_server.stats.read_elapsed_time);

                EXPECT_EQ(expected_stats.logical_write_count, log_server.stats.logical_write_count);
                EXPECT_EQ(expected_stats.garbage_collection_count, log_server.stats.garbage_collection_count);
                EXPECT_EQ(expected_stats.write_amplification, log_server.stats.write_amplification);
                EXPECT_EQ(expected_stats.utilization, log_server.stats.utilization);

                EXPECT_EQ(expected_stats.occupied_pages, log_server.stats.occupied_pages);

                EXPECT_EQ(expected_stats.block_erase_count, log_server.stats.block_erase_count);
                EXPECT_EQ(expected_stats.channel_switch_to_write, log_server.stats.channel_switch_to_write);
                EXPECT_EQ(expected_stats.channel_switch_to_read, log_server.stats.channel_switch_to_read);
            }
        }

        unsigned int time_per_write = devices[g_device_index].reg_write_delay + devices[g_device_index].cell_program_delay + devices[g_device_index].channel_switch_delay_w;
        unsigned int time_per_read = devices[g_device_index].reg_read_delay + devices[g_device_index].cell_read_delay + devices[g_device_index].channel_switch_delay_r;

        MONITOR_SYNC(g_device_index, &(log_server.stats), MONITOR_SLEEP_MAX_USEC);

        double write_speed = CALCULATEMBPS(ssd_config->get_page_size(), time_per_write);

        double read_speed = CALCULATEMBPS(ssd_config->get_page_size(), time_per_read);

        // checks that log_server.stats (the stats on the monitor) are accurate
        EXPECT_NEAR(write_speed, log_server.stats.write_speed, ERROR_THRESHHOLD(write_speed));
        EXPECT_NEAR(read_speed, log_server.stats.read_speed, ERROR_THRESHHOLD(read_speed));
        EXPECT_EQ(ssd_config->get_pages_ns(ID_SECTOR_NS0) + ssd_config->get_pages_ns(ID_SECTOR_NS1), log_server.stats.read_count);
        EXPECT_EQ(ssd_config->get_pages_ns(ID_SECTOR_NS0) + ssd_config->get_pages_ns(ID_SECTOR_NS1), log_server.stats.write_count);
        EXPECT_EQ(1, log_server.stats.write_amplification);
        EXPECT_EQ(0.8, log_server.stats.utilization);
        EXPECT_EQ(0, log_server.stats.garbage_collection_count);

        for (unsigned int p = 0; p < ssd_config->get_pages_ns(ID_SECTOR_NS0); p++)
        {
            FTL_WRITE_SECT(g_device_index, ID_SECTOR_NS0, p * ssd_config->get_page_size(), 1, NULL);

            action_count++;

            expected_stats.logical_write_count++;
            expected_stats.garbage_collection_count++;
        }

        for (unsigned int p = 0; p < ssd_config->get_pages_ns(ID_SECTOR_NS1); p++)
        {
            FTL_WRITE_SECT(g_device_index, ID_SECTOR_NS1, p * ssd_config->get_page_size(), 1, NULL);

            action_count++;

            expected_stats.logical_write_count++;
            expected_stats.garbage_collection_count++;

            if (action_count >= check_trigger)
            {
                action_count = 0;

                MONITOR_SYNC(g_device_index, &(log_server.stats), MONITOR_SLEEP_MAX_USEC);

                EXPECT_LE(0.8, log_server.stats.utilization);
                EXPECT_GE(1, log_server.stats.utilization);

                EXPECT_LE(1, log_server.stats.write_amplification);

                EXPECT_GE(expected_stats.garbage_collection_count, log_server.stats.garbage_collection_count);

                EXPECT_EQ(expected_stats.logical_write_count, log_server.stats.logical_write_count);
                EXPECT_LE(expected_stats.occupied_pages, log_server.stats.occupied_pages);
            }
        };

        MONITOR_SYNC(g_device_index, &(log_server.stats), MONITOR_SLEEP_MAX_USEC);

        EXPECT_LE(0.8, log_server.stats.utilization);
        EXPECT_GE(1, log_server.stats.utilization);

        EXPECT_LE(1, log_server.stats.write_amplification);

        EXPECT_GE(expected_stats.garbage_collection_count, log_server.stats.garbage_collection_count);

        EXPECT_EQ(expected_stats.logical_write_count, log_server.stats.logical_write_count);
        EXPECT_LE(expected_stats.occupied_pages, log_server.stats.occupied_pages);

        PINFO("finished WRITEREADWRITETest with stats :\n");
        printSSDStat(&log_server.stats);
    }

    /**
     * Test full capacity write to ALL namespaces in the disk and verify data integrity
     */
    TEST_P(WriteReadTest, FullCapacityAllNamespacesTest)
    {
        SSDConf *ssd_config = base_test_get_ssd_config();
        size_t pageSize = ssd_config->get_page_size();
        
        uint8_t* write_buffer = (uint8_t*)malloc(pageSize);
        uint8_t* read_buffer = (uint8_t*)malloc(pageSize);

        for (uint32_t nsid = 0; nsid < ssd_config->get_num_namespaces(); nsid++)
        {
            size_t totalPages = ssd_config->get_pages_ns(nsid);

            if (totalPages > 0 && ssd_config->get_ns_strategy(nsid) == FTL_NS_SECTOR)
            {
                PINFO("Testing Full Capacity for Sector NS %u (%lu pages)\n", nsid, totalPages);
                for (size_t p = 0; p < totalPages; p++)
                {
                    memset(write_buffer, (uint8_t)(p % 255), pageSize);
                    ASSERT_EQ(FTL_SUCCESS, FTL_WRITE_SECT(g_device_index, nsid, p * pageSize, pageSize, write_buffer));
                }

                for (size_t p = 0; p < totalPages; p++)
                {
                    memset(write_buffer, (uint8_t)(p % 255), pageSize);
                    ASSERT_EQ(FTL_SUCCESS, FTL_READ_SECT(g_device_index, nsid, p * pageSize, pageSize, read_buffer));
                    ASSERT_EQ(0, memcmp(write_buffer, read_buffer, pageSize)) << "Data corruption in NS " << nsid << " at page " << p;
                }
            }
        }

        free(write_buffer);
        free(read_buffer);
        PINFO("All Namespaces Full Capacity Test passed.\n");
    }

    /**
     * Verify that writing or reading beyond the namespace boundary returns an error
     */
    TEST_P(WriteReadTest, BoundaryViolationTest)
    {
        SSDConf *ssd_config = base_test_get_ssd_config();
        size_t pageSize = ssd_config->get_page_size();
        size_t totalPages = ssd_config->get_total_pages_ns(ID_SECTOR_NS0);

        uint8_t* buffer = (uint8_t*)malloc(pageSize);
        uint64_t invalid_offset = (uint64_t)totalPages * pageSize;

        PINFO("Testing boundary violation at offset: %lu (NS limit: %lu pages)\n", invalid_offset, totalPages);

        ASSERT_EQ(FTL_FAILURE, FTL_WRITE_SECT(g_device_index, ID_SECTOR_NS0, invalid_offset, pageSize, buffer));
        ASSERT_EQ(FTL_FAILURE, FTL_READ_SECT(g_device_index, ID_SECTOR_NS0, invalid_offset, pageSize, buffer));

        free(buffer);
        PINFO("Boundary Violation Test passed (Error correctly returned).\n");
    }

    /**
    * Worker function for parallel stress testing of a specific namespace
    */
    void ns_stress_worker(int device_idx, uint32_t nsid, size_t total_pages, size_t page_size)
    {
        uint8_t* write_buffer = (uint8_t*)malloc(page_size);
        uint8_t* read_buffer = (uint8_t*)malloc(page_size);

        for (int loop = 0; loop < 3; loop++)
        {
            for (size_t p = 0; p < total_pages; p++)
            {
                memset(write_buffer, (uint8_t)((p + nsid + loop) % 255), page_size);
                ASSERT_EQ(FTL_SUCCESS, FTL_WRITE_SECT(device_idx, nsid, p * page_size, page_size, write_buffer));
            }
            
            for (size_t p = 0; p < total_pages; p++)
            {
                memset(write_buffer, (uint8_t)((p + nsid + loop) % 255), page_size);
                ASSERT_EQ(FTL_SUCCESS, FTL_READ_SECT(device_idx, nsid, p * page_size, page_size, read_buffer));
                ASSERT_EQ(0, memcmp(write_buffer, read_buffer, page_size)) << "Data corruption in NS " << nsid << " at page " << p;
            }
        }

        free(write_buffer);
        free(read_buffer);
    }

    /**
    * Parallel Stress Test: Execute simultaneous writes and reads across all sector namespaces
    */
    TEST_P(WriteReadTest, ParallelNamespacesTest)
    {
        SSDConf *ssd_config = base_test_get_ssd_config();
        size_t pageSize = ssd_config->get_page_size();
        std::vector<std::thread> threads;

        PINFO("Starting Parallel Stress Test...\n");

        for (uint32_t nsid = 0; nsid < ssd_config->get_num_namespaces(); nsid++)
        {
            size_t pages = ssd_config->get_pages_ns(nsid);
            
            if (pages > 0 && ssd_config->get_ns_strategy(nsid) == FTL_NS_SECTOR)
            {
                size_t stress_pages = (pages > 100) ? 100 : pages;
                threads.emplace_back(ns_stress_worker, g_device_index, nsid, stress_pages, pageSize);
            }
        }

        for (auto& t : threads)
        {
            if (t.joinable()) {
                t.join();
            }
        }

        PINFO("Parallel Stress Test finished successfully.\n");
    }

} // namespace
