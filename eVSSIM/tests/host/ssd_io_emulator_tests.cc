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

// New browser delay values
extern int write_wall_time;
extern int read_wall_time;

#define MONITOR_SYNC_DELAY_USEC 150000
#define DELAY_THRESHOLD 2

/**
 * Wait for new monitor to be sync
 */
void MONITOR_STATS_UPDATE(int expected_duration) {
    usleep(expected_duration + MONITOR_SYNC_DELAY_USEC);
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
     * - execute 2 sequential page write
     * - execute 2 sequential page read
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
        SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,offset, WRITE, IO_PAGE_NB);
        SSD_PAGE_READ(flash_nb,block_nb,page_nb,offset, READ, IO_PAGE_NB);
        SSD_PAGE_READ(flash_nb,block_nb,page_nb,offset, READ, IO_PAGE_NB);
        SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,offset, WRITE, IO_PAGE_NB);

        // single write page delay
        int expected_write_duration = CHANNEL_SWITCH_DELAY_W + (REG_WRITE_DELAY + CELL_PROGRAM_DELAY) * 3;
        int expected_read_duration = CHANNEL_SWITCH_DELAY_R + (REG_READ_DELAY + CELL_READ_DELAY) * 2;

        // monitor statistics update
        MONITOR_STATS_UPDATE(expected_write_duration + expected_read_duration);

        // new monitor write delay
        ASSERT_LE(abs(ssd.current_stats->write_wall_time - expected_write_duration), DELAY_THRESHOLD);
        // new monitor read delay
        ASSERT_LE(abs(ssd.current_stats->read_wall_time - expected_read_duration), DELAY_THRESHOLD);
        // QT monitor write delay
        ASSERT_LE(abs((int)total_write_delay - expected_write_duration), DELAY_THRESHOLD);
        // QT monitor read delay
        ASSERT_LE(abs((int)total_read_delay - expected_read_duration), DELAY_THRESHOLD);
    }

    /**
     * @brief testing delay caused full disk writes
     * - write all pages in disk
     * - read all pages in disk
     * - verify QT monitor total write bandwidth delay is \expected_write_duration
     * - verify Browser monitor total write bandwidth delay is \expected_write_duration
     */
    TEST_P(SSDIoEmulatorUnitTest, BWCase2) {

        int offset = 0;
        int i,j,k;
        for(i = 0; i < FLASH_NB; i++) {
            for(j = 0; j <BLOCK_NB; j++) {
                for(k = 0; k < PAGE_NB; k++) {
                    SSD_PAGE_WRITE(i,j,k,offset, WRITE, IO_PAGE_NB);
                }
            }
        }

        for(i = 0; i < FLASH_NB; i++) {
            for(j = 0; j < BLOCK_NB; j++) {
                for(k = 0; k < PAGE_NB; k++) {
                    SSD_PAGE_READ(i,j,k,offset, READ, IO_PAGE_NB);
                }
            }
        }

        // single write page delay
        int expected_write_duration = (REG_WRITE_DELAY + CELL_PROGRAM_DELAY)
                * FLASH_NB * BLOCK_NB * PAGE_NB;
        int expected_read_duration = CHANNEL_SWITCH_DELAY_R * FLASH_NB
                + (REG_READ_DELAY + CELL_READ_DELAY) * FLASH_NB * BLOCK_NB * PAGE_NB;

        // wait for new monitor sync
        MONITOR_STATS_UPDATE(expected_write_duration + expected_read_duration);

        // new monitor write delay
        ASSERT_LE(abs(ssd.current_stats->write_wall_time - expected_write_duration), DELAY_THRESHOLD);
        // new monitor read delay
        ASSERT_LE(abs(ssd.current_stats->read_wall_time - expected_read_duration), DELAY_THRESHOLD);
        // TODO un-comment on fix old monitor delays
//        // QT monitor write delay
//        ASSERT_LE(abs((int)total_write_delay - expected_write_duration), DELAY_THRESHOLD);
//        // QT monitor read delay
//        ASSERT_LE(abs((int)total_read_delay - expected_read_duration), DELAY_THRESHOLD);
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

        // monitor statistics update
        MONITOR_STATS_UPDATE(expected_write_duration);
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

        // monitor statistics update
        MONITOR_STATS_UPDATE(expected_write_duration);
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

        // monitor statistics update
        MONITOR_STATS_UPDATE(expected_write_duration);
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

        // monitor statistics update
        MONITOR_STATS_UPDATE(expected_write_duration);
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

        // monitor statistics update
        MONITOR_STATS_UPDATE(expected_write_duration);
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

        int flash_nb_1 = 0;
        int flash_nb_2 = FLASH_NB > 0 ? 1 : 0;
        int block_nb_1 = 0;
        int block_nb_2 = BLOCK_NB > 0 ? 1 : 0;
        int page_nb = 0;
        int occupied_pages = 0;
        double ssd_utils = 0;

        int block_1_page_writes = PAGE_NB;
        int block_2_page_writes = PAGE_NB >= 1 ? PAGE_NB - 1 : 0;
        int block_3_page_writes = PAGE_NB >= 2 ? PAGE_NB - 2 : 0;

        int page_writes = block_1_page_writes + block_2_page_writes + block_3_page_writes;
        int expected_write_duration = (REG_WRITE_DELAY + CELL_PROGRAM_DELAY) * page_writes;

        int i;
        for( i = 0; i < block_1_page_writes; i++ )
            SSD_PAGE_WRITE(flash_nb_1,block_nb_1,page_nb + i,0, WRITE, IO_PAGE_NB);

        for( i = 0; i < block_2_page_writes; i++ )
            SSD_PAGE_WRITE(flash_nb_1,block_nb_2,page_nb + i,0, WRITE, IO_PAGE_NB);

        for( i = 0; i < block_3_page_writes; i++ )
            SSD_PAGE_WRITE(flash_nb_2,block_nb_1,page_nb + i,0, WRITE, IO_PAGE_NB);

        occupied_pages = page_writes;

        ssd_utils = (double)occupied_pages / PAGES_IN_SSD;
        // monitor statistics update
        MONITOR_STATS_UPDATE(expected_write_duration);
        // QT monitor
        ASSERT_EQ(ssd_utils, SSD_UTIL());
        // new monitor
        ASSERT_EQ(ssd_utils, ssd.current_stats->utilization);
        // execute erase
        SSD_BLOCK_ERASE(flash_nb_1,block_nb_2);
        // monitor statistics update
        MONITOR_STATS_UPDATE(BLOCK_ERASE_DELAY);
        // erase decrease occupied pages
        occupied_pages -= block_2_page_writes;
        ssd_utils = (double)occupied_pages / PAGES_IN_SSD;
        // QT monitor
        ASSERT_EQ(ssd_utils, SSD_UTIL());
        // new monitor
        ASSERT_EQ(ssd_utils, ssd.current_stats->utilization);
    }

    /**
     * @brief testing full ssd disk utilization
     * - write all pages in disk
     * - verify disk utilization is \disk_full
     * - erase all blocks in disk
     * - verify disk utilization is \disk_empty
     */
    TEST_P(SSDIoEmulatorUnitTest, SSDUtilCase7) {

        double disk_full = 1;
        double disk_empty = 0;

        int write_duration = (REG_WRITE_DELAY + CELL_PROGRAM_DELAY)
                * FLASH_NB * BLOCK_NB * PAGE_NB;
        int block_erase_duration = BLOCK_ERASE_DELAY * FLASH_NB * BLOCK_NB;

        int i, j, k;
        for( i = 0; i < FLASH_NB; i++ ) {
            for( j = 0; j < BLOCK_NB; j++ ) {
                for( k = 0; k < PAGE_NB; k++ ) {
                    SSD_PAGE_WRITE(i,j,k,0, WRITE, IO_PAGE_NB);
                }
            }
        }

        MONITOR_STATS_UPDATE(write_duration);
        // QT monitor
        ASSERT_EQ(disk_full, SSD_UTIL());
        // new monitor
        ASSERT_EQ(disk_full, ssd.current_stats->utilization);

        for( i = 0; i < FLASH_NB; i++ ) {
            for( j = 0; j < BLOCK_NB; j++ ) {
                SSD_BLOCK_ERASE(i,j);
            }
        }

        MONITOR_STATS_UPDATE(block_erase_duration);
        // QT monitor
        ASSERT_EQ(disk_empty, SSD_UTIL());
        // new monitor
        ASSERT_EQ(disk_empty, ssd.current_stats->utilization);
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
        // monitor statistics update
        MONITOR_STATS_UPDATE(expected_write_duration);

        ASSERT_EQ(ssd.physical_page_writes, physical_page_writes);
        ASSERT_EQ(ssd.current_stats->write_count, physical_page_writes);

        ASSERT_EQ(ssd.logical_page_writes, logical_page_writes);
        ASSERT_EQ(ssd.current_stats->logical_write_count, logical_page_writes);
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

        SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, GC_WRITE, GC_IO_PAGE_NB);

        // monitor statistics update
        MONITOR_STATS_UPDATE(expected_write_duration);
        ASSERT_EQ(physical_page_writes, ssd.physical_page_writes);
        ASSERT_EQ(physical_page_writes, ssd.current_stats->write_count);

        ASSERT_EQ(ssd.logical_page_writes, logical_page_writes);
        ASSERT_EQ(ssd.current_stats->logical_write_count, logical_page_writes);
    }

} //namespace
