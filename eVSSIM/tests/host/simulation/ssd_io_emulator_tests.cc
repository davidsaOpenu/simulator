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
#include <stdlib.h>

#define IO_PAGE_NB 0
#define GC_IO_PAGE_NB -1

extern LogServer log_server;
extern int servSock;
extern int clientSock;
extern int errno;

extern ssd_disk ssd;
extern write_amplification_counters wa_counters;
extern RTLogStatistics* rt_log_stats[MAX_DEVICES];

// New browser delay values
extern int write_elapsed_time;
extern int read_elapsed_time;

using namespace std;


namespace ssd_io_emulator_tests {

    class SSDIoEmulatorUnitTest : public BaseTest {
        public:
            virtual void SetUp() {
                BaseTest::SetUp();
                INIT_LOG_MANAGER(g_device_index);
            }

            virtual void TearDown() {
                BaseTest::TearDown();
                TERM_LOG_MANAGER(g_device_index);
            }
    };

    std::vector<SSDConf*> GetTestParams() {
        std::vector<SSDConf*> ssd_configs;

        ssd_configs.push_back(new SSDConf(parameters::Allsizemb[0]));

        return ssd_configs;
    }

    INSTANTIATE_TEST_CASE_P(DiskSize, SSDIoEmulatorUnitTest, ::testing::ValuesIn(GetTestParams()));

    /**
     * @brief testing delay caused by single page write
     * - execute 3 sequential page write
     * - verify Browser monitor total write bandwidth delay is \expected_write_duration
     */
    TEST_P(SSDIoEmulatorUnitTest, BWCaseWritesOnly1) {

        int flash_nb = 0;
        int block_nb = 0;
        int page_nb = 0;
        int offset = 0;
        int num_of_writes = 3;
        int write_elapsed_time = 0;

        SSD_PAGE_WRITE(g_device_index, flash_nb,block_nb,page_nb,offset, WRITE);
        SSD_PAGE_WRITE(g_device_index, flash_nb,block_nb,page_nb,offset, WRITE);
        SSD_PAGE_WRITE(g_device_index, flash_nb,block_nb,page_nb,offset, WRITE);

        // single write page delay
        int expected_write_duration = devices[g_device_index].channel_switch_delay_w+(devices[g_device_index].reg_write_delay + devices[g_device_index].cell_program_delay) * num_of_writes;

        // wait for new monitor sync
        MONITOR_SYNC_DELAY(expected_write_duration);

        uint32_t i;

        for (i = 0; i < devices[g_device_index].flash_nb; i++) {
            write_elapsed_time += rt_log_stats[g_device_index][i].write_elapsed_time;
        }
        // new monitor write delay
        ASSERT_LE(write_elapsed_time - expected_write_duration, DELAY_THRESHOLD);
    }

    /**
     * @brief testing delay caused by single page read
     * - execute 3 sequential page read
     * - verify Browser monitor total write bandwidth delay is \expected_read_duration
     */
    TEST_P(SSDIoEmulatorUnitTest, BWCaseReadsOnly1) {

        int flash_nb = 0;
        int block_nb = 0;
        int page_nb = 0;
        int offset = 0;
        int num_of_reads = 3;
        int read_elapsed_time = 0;

        SSD_PAGE_READ(g_device_index, flash_nb,block_nb,page_nb,offset, READ);
        SSD_PAGE_READ(g_device_index, flash_nb,block_nb,page_nb,offset, READ);
        SSD_PAGE_READ(g_device_index, flash_nb,block_nb,page_nb,offset, READ);

        // single read page delay
        int expected_read_duration = devices[g_device_index].channel_switch_delay_r + (devices[g_device_index].reg_read_delay + devices[g_device_index].cell_read_delay) * num_of_reads;

        // wait for new monitor sync
        MONITOR_SYNC_DELAY(expected_read_duration);

        uint32_t i;

        for (i = 0; i < devices[g_device_index].flash_nb; i++) {
            read_elapsed_time += rt_log_stats[g_device_index][i].read_elapsed_time;
        }
        // new monitor read delay
        ASSERT_LE(read_elapsed_time - expected_read_duration, DELAY_THRESHOLD);
    }

    /**
     * @brief testing delay caused by single page write
     * - execute 2 sequential page write
     * - execute 2 sequential page read
     * - execute page write
     * - verify Browser monitor total write bandwidth delay is \expected_write_duration
     */
    TEST_P(SSDIoEmulatorUnitTest, BWCase1) {

        int flash_nb = 0;
        int block_nb = 0;
        int page_nb = 0;
        int offset = 0;
        int write_elapsed_time = 0;
        int read_elapsed_time = 0;

        int expected_write_duration = 0;
        int expected_read_duration = 0;

        SSD_PAGE_WRITE(g_device_index, flash_nb,block_nb,page_nb,offset, WRITE);
        expected_write_duration += devices[g_device_index].channel_switch_delay_w + (devices[g_device_index].reg_write_delay + devices[g_device_index].cell_program_delay);

        SSD_PAGE_WRITE(g_device_index, flash_nb,block_nb,page_nb,offset, WRITE);
        expected_write_duration += (devices[g_device_index].reg_write_delay + devices[g_device_index].cell_program_delay);

        SSD_PAGE_READ(g_device_index, flash_nb,block_nb,page_nb,offset, READ);
        expected_read_duration += devices[g_device_index].channel_switch_delay_r + (devices[g_device_index].reg_read_delay + devices[g_device_index].cell_read_delay);

        SSD_PAGE_READ(g_device_index, flash_nb,block_nb,page_nb,offset, READ);
        expected_read_duration +=  (devices[g_device_index].reg_read_delay + devices[g_device_index].cell_read_delay);

        SSD_PAGE_WRITE(g_device_index, flash_nb,block_nb,page_nb,offset, WRITE);
        expected_write_duration += devices[g_device_index].channel_switch_delay_w + (devices[g_device_index].reg_write_delay + devices[g_device_index].cell_program_delay);


        // wait for new monitor sync
        MONITOR_SYNC_DELAY(expected_write_duration + expected_read_duration);

        uint32_t i;

        for (i = 0; i < devices[g_device_index].flash_nb; i++) {
            write_elapsed_time += rt_log_stats[g_device_index][i].write_elapsed_time;
            read_elapsed_time += rt_log_stats[g_device_index][i].read_elapsed_time;
        }
        // new monitor write delay
        ASSERT_LE(write_elapsed_time - expected_write_duration, DELAY_THRESHOLD);
        // new monitor read delay
        ASSERT_LE(read_elapsed_time - expected_read_duration, DELAY_THRESHOLD);
    }

    /**
     * @brief testing ssd utilization caused by occupied page
     * - execute page write
     * - verify QT monitor ssd utilization equals \ssd_utils
     * - verify new monitor ssd utilization equals \ssd_utils
     */
    TEST_P(SSDIoEmulatorUnitTest, SSDUtilCase1) {
        int flash_nb = 0;
        int block_nb = 0;
        int page_nb = 0;
        int occupied_pages = 1;
        double ssd_utils = (double)occupied_pages / devices[g_device_index].pages_in_ssd;
        int expected_write_duration = devices[g_device_index].reg_write_delay + devices[g_device_index].cell_program_delay + devices[g_device_index].channel_switch_delay_w;

        SSD_PAGE_WRITE(g_device_index, flash_nb,block_nb,page_nb,0, WRITE);

        // wait for new monitor sync
        MONITOR_SYNC_DELAY(expected_write_duration);
        // QT monitor
        ASSERT_EQ(ssd_utils, SSD_UTIL(g_device_index));
        // new monitor
        ASSERT_EQ(ssd_utils, log_server.stats.utilization);
    }

    /**
     * @brief testing ssd utilization caused by occupied page by non-logical write
     * - execute non-logical page write
     * - verify QT monitor ssd utilization equals \ssd_utils
     * - verify new monitor ssd utilization equals \ssd_utils
     */
    TEST_P(SSDIoEmulatorUnitTest, SSDUtilCase2) {
        int flash_nb = 0;
        int block_nb = 0;
        int page_nb = 0;
        int occupied_pages = 1;
        double ssd_utils = (double)occupied_pages / devices[g_device_index].pages_in_ssd;
        int expected_write_duration = devices[g_device_index].reg_write_delay + devices[g_device_index].cell_program_delay;

        SSD_PAGE_WRITE(g_device_index, flash_nb,block_nb,page_nb,0, GC_WRITE);

        // wait for new monitor sync
        MONITOR_SYNC_DELAY(expected_write_duration);
        // QT monitor
        ASSERT_EQ(ssd_utils, SSD_UTIL(g_device_index));
        // new monitor
        ASSERT_EQ(ssd_utils, log_server.stats.utilization);
    }

    /**
     * @brief testing ssd utilization block erase affect
     * - execute page write on block \block_nb
     * - erase block \block_nb
     * - verify QT monitor ssd utilization equals \ssd_utils
     * - verify new monitor ssd utilization equals \ssd_utils
     */
    TEST_P(SSDIoEmulatorUnitTest, SSDUtilCase3) {
        int flash_nb = 0;
        int block_nb = 0;
        int page_nb = 0;
        int expected_write_duration = devices[g_device_index].reg_write_delay + devices[g_device_index].cell_program_delay + devices[g_device_index].block_erase_delay;

        SSD_PAGE_WRITE(g_device_index, flash_nb, block_nb,page_nb,0, WRITE);
        GET_INVERSE_BLOCK_MAPPING_ENTRY(g_device_index, flash_nb, block_nb)->valid_page_nb = 1;//update mapping info
        SSD_BLOCK_ERASE(g_device_index, flash_nb, block_nb);

        // wait for new monitor sync
        MONITOR_SYNC_DELAY(expected_write_duration);
        // QT monitor
        ASSERT_EQ(0, SSD_UTIL(g_device_index));
        // new monitor
        ASSERT_EQ(0, log_server.stats.utilization);
    }

    /**
     * @brief testing affect of partial blocks erase no ssd utilization
     * - execute page write on block \block_nb
     * - execute page write on block \block_nb + 1
     * - verify ssd utilization match for 2 occupied pages
     * - erase block \block_nb
     * - verify QT monitor ssd utilization equals \ssd_utils
     * - verify new monitor ssd utilization equals \ssd_utils
     */
    TEST_P(SSDIoEmulatorUnitTest, SSDUtilCase4) {

        int flash_nb = 0;
        int block_nb = 0;
        int page_nb = 0;
        int occupied_pages = 0;
        double ssd_utils = 0;
        int expected_write_duration = (devices[g_device_index].channel_switch_delay_w+devices[g_device_index].reg_write_delay + devices[g_device_index].cell_program_delay) * 2;

        SSD_PAGE_WRITE(g_device_index, flash_nb,block_nb,page_nb,0, WRITE);
        SSD_PAGE_WRITE(g_device_index, flash_nb,block_nb+1,page_nb,0, WRITE);
        occupied_pages = 2;
        ASSERT_EQ((double)occupied_pages / devices[g_device_index].pages_in_ssd, SSD_UTIL(g_device_index));
        SSD_BLOCK_ERASE(g_device_index, flash_nb, block_nb);
        occupied_pages = 1;

        ssd_utils = (double)occupied_pages / devices[g_device_index].pages_in_ssd;

        // wait for new monitor sync
        MONITOR_SYNC_DELAY(expected_write_duration+devices[g_device_index].block_erase_delay);

        // QT monitor
        ASSERT_EQ(ssd_utils, SSD_UTIL(g_device_index));
        // new monitor
        ASSERT_EQ(ssd_utils, log_server.stats.utilization);
    }


    /**
     * @brief testing write page counters after single logical page write
     * - execute page write
     * - verify physical page write counter equals \physical_page_writes
     * - verify logical page write counter equals \logica_page_writes
     */
    TEST_P(SSDIoEmulatorUnitTest, CountersCase1) {

        int flash_nb = 0;
        int block_nb = 0;
        int page_nb = 0;
        unsigned int logical_write_count = 0;
        int logical_page_writes = 1;
        int physical_page_writes = 1;
        int expected_write_duration = devices[g_device_index].channel_switch_delay_w + devices[g_device_index].reg_write_delay + devices[g_device_index].cell_program_delay;

        SSD_PAGE_WRITE(g_device_index, flash_nb,block_nb,page_nb,0, WRITE);
        // wait for new monitor sync
        MONITOR_SYNC_DELAY(expected_write_duration);

        uint32_t i;

        for (i = 0; i < devices[g_device_index].flash_nb; i++) {
            logical_write_count += rt_log_stats[g_device_index][i].logical_write_count;
        }

        ASSERT_EQ(ssds_manager[g_device_index].ssd.physical_page_writes, physical_page_writes);
        ASSERT_EQ(ssds_manager[g_device_index].ssd.current_stats->write_count, physical_page_writes);

        ASSERT_EQ(ssds_manager[g_device_index].ssd.logical_page_writes, logical_page_writes);
        ASSERT_EQ(logical_write_count, logical_page_writes);
    }

    /**
     * @brief testing write page counters after single non-logical page write
     * - execute gc page write
     * - verify physical page write counter equals \physical_page_writes
     * - verify logical page write counter equals \logica_page_writes
     */
    TEST_P(SSDIoEmulatorUnitTest, CountersCase2) {

        int flash_nb = 0;
        int block_nb = 0;
        int page_nb = 0;
        unsigned int logical_write_count = 0;

        int logical_page_writes = 0;
        int physical_page_writes = 1;
        int expected_write_duration = devices[g_device_index].channel_switch_delay_w + devices[g_device_index].reg_write_delay + devices[g_device_index].cell_program_delay;

        // wait for new monitor sync
        SSD_PAGE_WRITE(g_device_index, flash_nb,block_nb,page_nb,0, GC_WRITE);

        MONITOR_SYNC_DELAY(expected_write_duration);

        uint32_t i;

        for (i = 0; i < devices[g_device_index].flash_nb; i++) {
            logical_write_count += rt_log_stats[g_device_index][i].logical_write_count;
        }

        ASSERT_EQ(physical_page_writes, ssds_manager[g_device_index].ssd.physical_page_writes);
        ASSERT_EQ(physical_page_writes, ssds_manager[g_device_index].ssd.current_stats->write_count);

        ASSERT_EQ(ssds_manager[g_device_index].ssd.logical_page_writes, logical_page_writes);
        ASSERT_EQ(logical_write_count, logical_page_writes);
    }

    /**
     * testing the statistics mechanism by issuing a full block write/read and validate them
     * - issue full block write
     * - issue full block read
     * - validate statistics
     */
    TEST_P(SSDIoEmulatorUnitTest, WriteRead1) {
        SSDConf* ssd_config = base_test_get_ssd_config();

        int flash_nb = 0;
        int block_nb = 0;
        int expected_write_duration = (devices[g_device_index].reg_write_delay + devices[g_device_index].cell_program_delay) * ssd_config->get_pages_per_block();
        int expected_read_duration = devices[g_device_index].channel_switch_delay_r + (devices[g_device_index].reg_read_delay + devices[g_device_index].cell_read_delay) * ssd_config->get_pages_per_block();
        int expected_rw = ssd_config->get_pages_per_block();

        // Write all pages in the block
        for (size_t i = 0; i < ssd_config->get_pages_per_block(); i++) {
            SSD_PAGE_WRITE(g_device_index, flash_nb, block_nb, i, 0, WRITE);
        }

        // Read all pages in the block
        for (size_t i = 0; i < ssd_config->get_pages_per_block(); i++) {
            SSD_PAGE_READ(g_device_index, flash_nb, block_nb, i, 0, READ);
        }

        MONITOR_SYNC_DELAY(expected_write_duration + expected_read_duration);

        ASSERT_EQ(expected_rw, ssds_manager[g_device_index].ssd.current_stats->write_count);
        ASSERT_EQ(expected_rw, ssds_manager[g_device_index].ssd.current_stats->read_count);

    }

    /**
     * testing the statistics mechanism by issuing a full channel write/read and validate them
     * - issue full channel write
     * - issue full channel read
     * - validate statistics
     */
    TEST_P(SSDIoEmulatorUnitTest, WriteRead2) {
        SSDConf* ssd_config = base_test_get_ssd_config();

        size_t flash_num = ssd_config->get_flash_nb();
        size_t block_x_flash = ssd_config->get_pages() / ssd_config->get_pages_per_block();
        size_t blocks_per_flash = block_x_flash / flash_num;

        int expected_rw = ssd_config->get_pages_per_block() * blocks_per_flash;
        int expected_write_duration = (devices[g_device_index].channel_switch_delay_w + devices[g_device_index].reg_write_delay + devices[g_device_index].cell_program_delay) * expected_rw;
        int expected_read_duration = (devices[g_device_index].channel_switch_delay_r + devices[g_device_index].reg_read_delay + devices[g_device_index].cell_read_delay) * expected_rw;


        // Write all blocks in the channel
        for (size_t i = 0; i < blocks_per_flash; i++) {
            for (size_t j = 0; j < ssd_config->get_pages_per_block(); j++) {
                SSD_PAGE_WRITE(g_device_index, 0, i, j, 0, WRITE);
            }
        }

        // Read all blocks in the channel
        for (size_t i = 0; i < blocks_per_flash; i++) {
            for (size_t j = 0; j < ssd_config->get_pages_per_block(); j++) {
                SSD_PAGE_READ(g_device_index, 0, i, j, 0, READ);
            }
        }

        MONITOR_SYNC_DELAY(expected_write_duration + expected_read_duration);

        ASSERT_EQ(expected_rw, ssds_manager[g_device_index].ssd.current_stats->write_count);
        ASSERT_EQ(expected_rw, ssds_manager[g_device_index].ssd.current_stats->read_count);

    }

    /**
     * testing the write amplification calculation by writing over the whole flash twice
     * - write over flash twice using the FTL layer
     * - validate statistics
     */
    TEST_P(SSDIoEmulatorUnitTest, WriteAmplificationTest) {
        SSDConf* ssd_config = base_test_get_ssd_config();
        int expected_write_amplification = 1;

        const size_t page_x_flash = (ssd_config->get_pages());

        // Write all flash.
        for(int x=0; x<2; x++){
            for(size_t p=0; p < page_x_flash; p++){
                ASSERT_EQ(FTL_SUCCESS, _FTL_WRITE_SECT(g_device_index, p * ssd_config->get_page_size(), 1, NULL));
            }
            MONITOR_SYNC_DELAY(15000000);

            //at most one block that wasn't cleared by GC algorithem
            double error_util = (double)(ssd_config->get_pages_per_block()) / (ssd_config->get_page_nb() * ssd_config->get_block_nb() * ssd_config->get_flash_nb());
            ASSERT_NEAR(0.8, ssds_manager[g_device_index].ssd.current_stats->utilization, error_util); // 25% over provitioning = 80% full

            ASSERT_EQ(page_x_flash * (x + 1), ssds_manager[g_device_index].ssd.current_stats->logical_write_count);
            ASSERT_LE(page_x_flash * (x + 1), ssds_manager[g_device_index].ssd.current_stats->write_count);
        }

        int expected_write_duration = (devices[g_device_index].channel_switch_delay_w + devices[g_device_index].reg_write_delay + devices[g_device_index].cell_program_delay) * page_x_flash * 2;

        MONITOR_SYNC_DELAY(expected_write_duration);

        // Assert w.a. is greater then 1
        ASSERT_GE(page_x_flash, ssds_manager[g_device_index].ssd.current_stats->garbage_collection_count);

        int expected_write_amplification = 1;

        //write amp = 1 because we work with over-provitioning and write sequentionally, on the second pass
        //we re-allocate the first block, when we get to the second block, there is now a free block that can be used
        //for re-allocating the second block
        ASSERT_LE(expected_write_amplification, ssds_manager[g_device_index].ssd.current_stats->write_amplification);
    }

} //namespace
