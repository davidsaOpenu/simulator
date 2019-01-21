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
#include "ssd_log_manager.h"

// default value for flash number
#define DEFAULT_FLASH_NB 4
#define IO_PAGE_NB 0
#define GC_IO_PAGE_NB -1

extern LogServer log_server;
extern int servSock;
extern int clientSock;
//logger_monitor log_monitor;
extern int errno;

// QT monitor delay values
extern double total_write_delay;
extern double total_read_delay;

extern ssd_disk ssd;
extern write_amplification_counters wa_counters;
RTLogStatistics rt_log_stats;
extern LoggerAnalyzerStorage* analyzers_storage;

// New browser delay values
extern int write_wall_time;
extern int read_wall_time;
//extern unsigned int logical_write_count;

#define MONITOR_SYNC_DELAY_USEC 150000
#define DELAY_THRESHOLD 2

/**
 * Wait for new monitor to be sync & gathering new monitor
 * statistics into single statistic object
 */
void MONITOR_SYNC_DELAY(int expected_duration) {

    usleep(expected_duration + MONITOR_SYNC_DELAY_USEC);

    rt_log_stats.logical_write_count = 0;
    rt_log_stats.write_wall_time = 0;
    rt_log_stats.read_wall_time = 0;
    rt_log_stats.current_wall_time = 0;
    rt_log_stats.occupied_pages = 0;

    /**
     * Gathering rt_log_stats from all flash into gathered rt_log_stats
     */
    int i;
    for( i = 0; i < FLASH_NB; i++ ) {

        RTLogStatistics flash_rt_log_stats = analyzers_storage[i].logger->rt_log_stats;
        rt_log_stats.logical_write_count += flash_rt_log_stats.logical_write_count;
        rt_log_stats.write_wall_time += flash_rt_log_stats.write_wall_time;
        rt_log_stats.read_wall_time += flash_rt_log_stats.read_wall_time;
        rt_log_stats.current_wall_time += flash_rt_log_stats.current_wall_time;
        rt_log_stats.occupied_pages += flash_rt_log_stats.occupied_pages;
    }
}

int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

using namespace std;

namespace {

    class SSDIoEmulatorUnitTest : public BaseEmulatorTests {};

    std::vector<std::pair<size_t,size_t> > GetParams() {
        std::vector<std::pair<size_t,size_t> > list;
        const int constFlashNum = DEFAULT_FLASH_NB;
        unsigned int i = 0;
        list.push_back(std::make_pair(parameters::Allsizemb[i], constFlashNum ));
        return list;
    }

    INSTANTIATE_TEST_CASE_P(DiskSize, SSDIoEmulatorUnitTest, ::testing::ValuesIn(GetParams()));

    /**
     * @brief testing delay caused by single page write
     * - execute page write
     * - verify QT monitor total write bandwidth delay is \expected_write_duration
     * - verify Browser monitor total write bandwidth delay is \expected_write_duration
     */
    TEST_P(SSDIoEmulatorUnitTest, BWCase1) {

        int flash_nb = 0;
        int block_nb = 0;
        int page_nb = 0;
        int offset = 0;

        SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,offset, WRITE, IO_PAGE_NB);
        SSD_PAGE_READ(flash_nb,block_nb,page_nb,offset, READ, IO_PAGE_NB);

        // single write page delay
        int expected_write_duration = REG_WRITE_DELAY + CELL_PROGRAM_DELAY;
        int expected_read_duration = CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY + CELL_READ_DELAY;

        // wait for new monitor sync
        MONITOR_SYNC_DELAY(expected_write_duration + expected_read_duration);
        // new monitor write delay
        ASSERT_LE(abs(rt_log_stats.write_wall_time - expected_write_duration), DELAY_THRESHOLD);
        // new monitor read delay
        ASSERT_LE(abs(rt_log_stats.read_wall_time - expected_read_duration), DELAY_THRESHOLD);
        // QT monitor write delay
        ASSERT_LE(abs((int)total_write_delay - expected_write_duration), DELAY_THRESHOLD);
        // QT monitor read delay
        ASSERT_LE(abs((int)total_read_delay - expected_read_duration), DELAY_THRESHOLD);
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
        double ssd_utils = (double)occupied_pages / PAGES_IN_SSD;
        int expected_write_duration = REG_WRITE_DELAY + CELL_PROGRAM_DELAY;

        SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, IO_PAGE_NB);

        // wait for new monitor sync
        MONITOR_SYNC_DELAY(expected_write_duration);
        // QT monitor
        ASSERT_EQ(ssd_utils, SSD_UTIL());
        // new monitor
        ASSERT_EQ(ssd_utils, ssd.current_stats->utilization);
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
        double ssd_utils = (double)occupied_pages / PAGES_IN_SSD;
        int expected_write_duration = REG_WRITE_DELAY + CELL_PROGRAM_DELAY;

        SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, GC_WRITE, GC_IO_PAGE_NB);

        // wait for new monitor sync
        MONITOR_SYNC_DELAY(expected_write_duration);
        // QT monitor
        ASSERT_EQ(ssd_utils, SSD_UTIL());
        // new monitor
        ASSERT_EQ(ssd_utils, ssd.current_stats->utilization);
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
        int occupied_pages = 0;
        double ssd_utils = (double)occupied_pages / PAGES_IN_SSD;
        int expected_write_duration = REG_WRITE_DELAY + CELL_PROGRAM_DELAY + BLOCK_ERASE_DELAY;

        SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, IO_PAGE_NB);
        SSD_BLOCK_ERASE(flash_nb,block_nb);

        // wait for new monitor sync
        MONITOR_SYNC_DELAY(expected_write_duration);
        // QT monitor
        ASSERT_EQ(ssd_utils, SSD_UTIL());
        // new monitor
        ASSERT_EQ(ssd_utils, ssd.current_stats->utilization);
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
        int expected_write_duration = (REG_WRITE_DELAY + CELL_PROGRAM_DELAY) * 2;

        SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, IO_PAGE_NB);
        SSD_PAGE_WRITE(flash_nb,block_nb+1,page_nb,0, WRITE, IO_PAGE_NB);
        occupied_pages = 2;
        ASSERT_EQ((double)occupied_pages / PAGES_IN_SSD, SSD_UTIL());
        SSD_BLOCK_ERASE(flash_nb,block_nb);
        occupied_pages-=1;

        ssd_utils = (double)occupied_pages / PAGES_IN_SSD;

        // wait for new monitor sync
        MONITOR_SYNC_DELAY(expected_write_duration);
        // QT monitor
        ASSERT_EQ(ssd_utils, SSD_UTIL());
        // new monitor
        ASSERT_EQ(ssd_utils, ssd.current_stats->utilization);
    }

    /**
     * @brief testing affect of partial flash erase no ssd utilization
     * - execute page write on flash \flash_nb
     * - execute page write on flash \flash_nb
     * - execute page write on flash \flash_nb + 1
     * - verify ssd utilization match for 2 occupied pages
     * - erase flash \flash_nb
     * - verify QT monitor ssd utilization equals \ssd_utils
     * - verify new monitor ssd utilization equals \ssd_utils
     */
    TEST_P(SSDIoEmulatorUnitTest, SSDUtilCase5) {

        int flash_nb = 0;
        int block_nb = 0;
        int page_nb = 0;
        int occupied_pages = 0;
        double ssd_utils = 0;
        int expected_write_duration = (CHANNEL_SWITCH_DELAY_W + REG_WRITE_DELAY + CELL_PROGRAM_DELAY) * 3;

        SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, IO_PAGE_NB);
        SSD_PAGE_WRITE(flash_nb,block_nb,page_nb+1,0, WRITE, IO_PAGE_NB);
        SSD_PAGE_WRITE(flash_nb+1,block_nb,page_nb,0, WRITE, IO_PAGE_NB);

        occupied_pages = 3;
        ASSERT_EQ((double)occupied_pages / PAGES_IN_SSD, SSD_UTIL());
        SSD_BLOCK_ERASE(flash_nb,block_nb);
        occupied_pages-=2;

        ssd_utils = (double)occupied_pages / PAGES_IN_SSD;

        // wait for new monitor sync
        MONITOR_SYNC_DELAY(expected_write_duration);
        // QT monitor
        ASSERT_EQ(ssd_utils, SSD_UTIL());
        // new monitor
        ASSERT_EQ(ssd_utils, ssd.current_stats->utilization);
    }


    /**
     * @brief testing affect multiple page write on ssd utilization
     * - execute \block_1_page_writes page writes on flash \flash_nb and block \block_nb
     * - execute \block_2_page_writes page writes on flash \flash_nb and block \block_nb+1
     * - execute \block_3_page_writes page writes on flash \flash_nb+1 and block \block_nb
     * - verify ssd utilization match for \occupied_pages
     * - erase flash \flash_nb block \block_nb + 1
     * - verify QT monitor ssd utilization equals \ssd_utils
     * - verify new monitor ssd utilization equals \ssd_utils
     */
    TEST_P(SSDIoEmulatorUnitTest, SSDUtilCase6) {

        int flash_nb = 0;
        int block_nb = 0;
        int page_nb = 0;
        int occupied_pages = 0;
        double ssd_utils = 0;

        int block_1_page_writes = 16;
        int block_2_page_writes = 8;
        int block_3_page_writes = 4;

        int page_writes = block_1_page_writes + block_2_page_writes + block_3_page_writes;
        int expected_write_duration = (REG_WRITE_DELAY + CELL_PROGRAM_DELAY) * page_writes;

        int i;
        for( i = 0; i < block_1_page_writes; i++ )
            SSD_PAGE_WRITE(flash_nb,block_nb,page_nb + i,0, WRITE, IO_PAGE_NB);

        for( i = 0; i < block_2_page_writes; i++ )
            SSD_PAGE_WRITE(flash_nb,block_nb + 1,page_nb + i,0, WRITE, IO_PAGE_NB);

        for( i = 0; i < block_3_page_writes; i++ )
            SSD_PAGE_WRITE(flash_nb + 1,block_nb,page_nb,0, WRITE, IO_PAGE_NB);

        occupied_pages = page_writes;

        ssd_utils = (double)occupied_pages / PAGES_IN_SSD;
        // wait for new monitor sync
        MONITOR_SYNC_DELAY(expected_write_duration);
        // QT monitor
        ASSERT_EQ(ssd_utils, SSD_UTIL());
        // new monitor
        ASSERT_EQ(ssd_utils, ssd.current_stats->utilization);
        // execute erase
        SSD_BLOCK_ERASE(flash_nb,block_nb+1);
        // wait for new monitor sync
        MONITOR_SYNC_DELAY(BLOCK_ERASE_DELAY);
        // erase decrease occupied pages
        occupied_pages -= block_2_page_writes;
        ssd_utils = (double)occupied_pages / PAGES_IN_SSD;
        // QT monitor
        ASSERT_EQ(ssd_utils, SSD_UTIL());
        // new monitor
        ASSERT_EQ(ssd_utils, ssd.current_stats->utilization);
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
        int logical_page_writes = 1;
        int physical_page_writes = 1;
        int expected_write_duration = REG_WRITE_DELAY + CELL_PROGRAM_DELAY;

        SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, IO_PAGE_NB);
        // wait for new monitor sync
        MONITOR_SYNC_DELAY(expected_write_duration);

        ASSERT_EQ(ssd.physical_page_writes, physical_page_writes);
        ASSERT_EQ(ssd.current_stats->write_count, physical_page_writes);

        ASSERT_EQ(ssd.logical_page_writes, logical_page_writes);
        ASSERT_EQ(rt_log_stats.logical_write_count, logical_page_writes);
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

        int logical_page_writes = 0;
        int physical_page_writes = 1;
        int expected_write_duration = REG_WRITE_DELAY + CELL_PROGRAM_DELAY;

        // wait for new monitor sync
        SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, GC_WRITE, GC_IO_PAGE_NB);

        MONITOR_SYNC_DELAY(expected_write_duration);
        ASSERT_EQ(physical_page_writes, ssd.physical_page_writes);
        ASSERT_EQ(physical_page_writes, ssd.current_stats->write_count);

        ASSERT_EQ(ssd.logical_page_writes, logical_page_writes);
        ASSERT_EQ(rt_log_stats.logical_write_count, logical_page_writes);
    }

} //namespace
