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

extern RTLogStatistics* rt_log_stats[MAX_DEVICES];
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
            INIT_LOG_MANAGER(g_device_index);
        }

        virtual void TearDown()
        {
            BaseTest::TearDown();
            TERM_LOG_MANAGER(g_device_index);
        }
    };

    std::vector<SSDConf *> GetTestParams()
    {
        std::vector<SSDConf *> ssd_configs;

        for (unsigned int i = 1; i <= MAX_POW; i++)
        {
            ssd_configs.push_back(new SSDConf(pow(2, i), true));
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
        uint64_t check_trigger = (double)ssd_config->get_pages() * CHECK_THRESHOLD;

        SSDStatistics expected_stats = stats_init();
        expected_stats.write_amplification = 1;
        uint64_t prev_channele = -1;

        // Writes the whole ssd in the default namespace.
        for (size_t p = 0; p < ssd_config->get_pages(); p++)
        {
            _FTL_WRITE_SECT(g_device_index, p * ssd_config->get_page_size(), 1, NULL);

            action_count++;

            expected_stats.write_count++;
            expected_stats.occupied_pages++;
            expected_stats.logical_write_count++;

            expected_stats.write_elapsed_time += devices[g_device_index].reg_write_delay + devices[g_device_index].cell_program_delay;

            uint64_t cur_channele = CALC_FLASH(g_device_index, GET_MAPPING_INFO(g_device_index, p)) % devices[g_device_index].channel_nb;

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
        ASSERT_EQ(ssd_config->get_pages(), log_server.stats.write_count);
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
        uint64_t check_trigger = (double)ssd_config->get_pages() * CHECK_THRESHOLD;

        SSDStatistics expected_stats = stats_init();
        expected_stats.write_amplification = 1;
        uint64_t prev_channele = -1;

        // writes the whole ssd
        for (unsigned int p = 0; p < ssd_config->get_pages(); p++)
        {
            _FTL_WRITE_SECT(g_device_index, p * ssd_config->get_page_size(), 1, NULL);

            action_count++;

            expected_stats.write_count++;
            expected_stats.occupied_pages++;
            expected_stats.logical_write_count++;

            expected_stats.write_elapsed_time += devices[g_device_index].reg_write_delay + devices[g_device_index].cell_program_delay;

            uint64_t cur_channele = CALC_FLASH(g_device_index, GET_MAPPING_INFO(g_device_index, p)) % devices[g_device_index].channel_nb;

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
        // reads the whole ssd
        for (unsigned int p = 0; p < ssd_config->get_pages(); p++)
        {
            _FTL_READ_SECT(g_device_index, p * ssd_config->get_page_size(), 1, NULL);

            action_count++;

            expected_stats.read_count++;

            expected_stats.read_elapsed_time += devices[g_device_index].reg_read_delay + devices[g_device_index].cell_read_delay;

            uint64_t cur_channele = CALC_FLASH(g_device_index, GET_MAPPING_INFO(g_device_index, p)) % devices[g_device_index].channel_nb;

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
        ASSERT_EQ(ssd_config->get_pages(), log_server.stats.read_count);
        ASSERT_EQ(ssd_config->get_pages(), log_server.stats.write_count);
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
        uint64_t check_trigger = (double)ssd_config->get_pages() * CHECK_THRESHOLD;

        SSDStatistics expected_stats = stats_init();
        expected_stats.write_amplification = 1;

        // writes and reads pages one at a time

        for (unsigned int p = 0; p < ssd_config->get_pages(); p++)
        {
            _FTL_WRITE_SECT(g_device_index, p * ssd_config->get_page_size(), 1, NULL);
            _FTL_READ_SECT(g_device_index, p * ssd_config->get_page_size(), 1, NULL);

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
        ASSERT_EQ(ssd_config->get_pages(), log_server.stats.read_count);
        ASSERT_EQ(ssd_config->get_pages(), log_server.stats.write_count);
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
        uint64_t check_trigger = (double)ssd_config->get_pages() * CHECK_THRESHOLD;

        SSDStatistics expected_stats = stats_init();
        expected_stats.write_amplification = 1;

        uint64_t total_pages = ssd_config->get_pages(); // write to 80% of ssd so GC has empty pages to work with
        // writes and reads pages one at a time

        for (unsigned int p = 0; p < total_pages; p++)
        {
            _FTL_WRITE_SECT(g_device_index, p * ssd_config->get_page_size(), 1, NULL);
            _FTL_READ_SECT(g_device_index, p * ssd_config->get_page_size(), 1, NULL);

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
        unsigned int time_per_read = devices[g_device_index].reg_read_delay + devices[g_device_index].cell_read_delay + devices[g_device_index].channel_switch_delay_r;

        MONITOR_SYNC(g_device_index, &(log_server.stats), MONITOR_SLEEP_MAX_USEC);

        double write_speed = CALCULATEMBPS(ssd_config->get_page_size(), time_per_write);

        double read_speed = CALCULATEMBPS(ssd_config->get_page_size(), time_per_read);

        // checks that log_server.stats (the stats on the monitor) are accurate
        ASSERT_NEAR(write_speed, log_server.stats.write_speed, ERROR_THRESHHOLD(write_speed));
        ASSERT_NEAR(read_speed, log_server.stats.read_speed, ERROR_THRESHHOLD(read_speed));
        ASSERT_EQ(total_pages, log_server.stats.read_count);
        ASSERT_EQ(total_pages, log_server.stats.write_count);
        ASSERT_EQ(1, log_server.stats.write_amplification);
        ASSERT_EQ(0.8, log_server.stats.utilization);
        ASSERT_EQ(0, log_server.stats.garbage_collection_count);

        for (unsigned int p = 0; p < total_pages; p++)
        {
            _FTL_WRITE_SECT(g_device_index, p * ssd_config->get_page_size(), 1, NULL);

            action_count++;

            expected_stats.logical_write_count++;
            expected_stats.garbage_collection_count++;

            if (action_count >= check_trigger)
            {
                action_count = 0;

                MONITOR_SYNC(g_device_index, &(log_server.stats), MONITOR_SLEEP_MAX_USEC);

                ASSERT_LE(0.8, log_server.stats.utilization);
                ASSERT_GE(1, log_server.stats.utilization);

                ASSERT_LE(1, log_server.stats.write_amplification);

                ASSERT_GE(expected_stats.garbage_collection_count, log_server.stats.garbage_collection_count);

                ASSERT_EQ(expected_stats.logical_write_count, log_server.stats.logical_write_count);
                ASSERT_LE(expected_stats.occupied_pages, log_server.stats.occupied_pages);
            }
        };

        MONITOR_SYNC(g_device_index, &(log_server.stats), MONITOR_SLEEP_MAX_USEC);

        ASSERT_LE(0.8, log_server.stats.utilization);
        ASSERT_GE(1, log_server.stats.utilization);

        ASSERT_LE(1, log_server.stats.write_amplification);

        ASSERT_GE(expected_stats.garbage_collection_count, log_server.stats.garbage_collection_count);

        ASSERT_EQ(expected_stats.logical_write_count, log_server.stats.logical_write_count);
        ASSERT_LE(expected_stats.occupied_pages, log_server.stats.occupied_pages);

        PINFO("finished WRITEREADWRITETest with stats :\n");
        printSSDStat(&log_server.stats);
    }
} // namespace
