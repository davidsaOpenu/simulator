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

extern "C" {

#include "common.h"
#include "ftl_sect_strategy.h"
}
extern "C" int g_init;
extern "C" int clientSock;
extern "C" int g_init_log_server;
bool g_nightly_mode = false;

#define GTEST_DONT_DEFINE_FAIL 1
#include <gtest/gtest.h>

#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <sys/socket.h>
#include<pthread.h>
#include <arpa/inet.h>

//#include "ssd_log_monitor.h"

// default value for flash number
#define DEFAULT_FLASH_NB 4
#define IO_PAGE_FIRST -1

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
extern unsigned int logical_write_count;
extern SSDStatistics* current_stats;

#define MONITOR_SYNC_DELAY 150000
#define DELAY_THRESHOLD 2

namespace parameters
{
    enum sizemb
    {
        mb1 = 1,
        mb2 = 2,
        mb3 = 4,
        mb4 = 8
    };

    static const sizemb Allsizemb[] = { mb2 };

    enum flashnb
    {
        fnb1 = 2,
        fnb2 = 4,
        fnb3 = 8,
        fnb4 = 16,
        fnb5 = 32
    };

    static const flashnb Allflashnb[] = { fnb1 };
}

int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

using namespace std;

namespace {

    class SSDIoEmulatorUnitTest : public ::testing::TestWithParam<std::pair<size_t,size_t> > {
        public:

            //const static size_t CONST_BLOCK_NB_PER_FLASH = 4096;
            const static size_t CONST_PAGES_PER_BLOCK = 8; // external (non over-provisioned)
            const static size_t CONST_PAGES_PER_BLOCK_OVERPROV = (CONST_PAGES_PER_BLOCK * 25) / 100; // 25 % of pages for over-provisioning
            const static size_t CONST_PAGE_SIZE_IN_BYTES = 4096;

            virtual void SetUp() {
                std::pair<size_t,size_t> params = GetParam();
                size_t mb = params.first;
                size_t flash_nb = params.second;
                pages_= mb * ((1024 * 1024) / CONST_PAGE_SIZE_IN_BYTES); // number_of_pages = disk_size (in MB) * 1048576 / page_size
                size_t block_x_flash = pages_ / CONST_PAGES_PER_BLOCK; // all_blocks_on_all_flashes = number_of_pages / pages_in_block
                //size_t flash = block_x_flash / CONST_BLOCK_NB_PER_FLASH; // number_of_flashes = all_blocks_on_all_flashes / number_of_blocks_in_flash
                size_t blocks_per_flash = block_x_flash / flash_nb; // number_of_flashes = all_blocks_on_all_flashes / number_of_blocks_in_flash

                ofstream ssd_conf("data/ssd.conf", ios_base::out | ios_base::trunc);
                ssd_conf << "FILE_NAME ./data/ssd.img\n"
                    "PAGE_SIZE " << CONST_PAGE_SIZE_IN_BYTES << "\n"
                    "PAGE_NB " << (CONST_PAGES_PER_BLOCK + CONST_PAGES_PER_BLOCK_OVERPROV) << "\n"
                    "SECTOR_SIZE 1\n"
                    "FLASH_NB " << flash_nb << "\n"
                    "BLOCK_NB " << blocks_per_flash << "\n"
                    "PLANES_PER_FLASH 1\n"
                    "REG_WRITE_DELAY 82\n"
                    "CELL_PROGRAM_DELAY 900\n"
                    "REG_READ_DELAY 82\n"
                    "CELL_READ_DELAY 50\n"
                    "BLOCK_ERASE_DELAY 2\n"
                    "CHANNEL_SWITCH_DELAY_R 16\n"
                    "CHANNEL_SWITCH_DELAY_W 33\n"
                    "CHANNEL_NB " << flash_nb << "\n"
                    "STAT_TYPE 15\n"
                    "STAT_SCOPE 62\n"
                    "STAT_PATH /tmp/stat.csv\n"
                    "STORAGE_STRATEGY 1\n"; // sector strategy
                ssd_conf.close();
                FTL_INIT();
                INIT_LOG_MANAGER();
            }
            virtual void TearDown() {
                FTL_TERM();
                TERM_LOG_MANAGER();
                remove("data/empty_block_list.dat");
                remove("data/inverse_block_mapping.dat");
                remove("data/inverse_page_mapping.dat");
                remove("data/mapping_table.dat");
                remove("data/valid_array.dat");
                remove("data/victim_block_list.dat");
                remove("data/ssd.conf");
                g_init = 0;
            }
        protected:
            size_t pages_;
    };

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

        SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,offset, WRITE, IO_PAGE_FIRST);
        SSD_PAGE_READ(flash_nb,block_nb,page_nb,offset, READ, IO_PAGE_FIRST);

        // single write page delay
        int expected_write_duration = REG_WRITE_DELAY + CELL_PROGRAM_DELAY;
        int expected_read_duration = CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY + CELL_READ_DELAY;

        usleep(MONITOR_SYNC_DELAY);

        ASSERT_LE(expected_write_duration - DELAY_THRESHOLD, write_wall_time);
        ASSERT_GE(expected_write_duration + DELAY_THRESHOLD, write_wall_time);
        ASSERT_LE(expected_read_duration - DELAY_THRESHOLD, read_wall_time);
        ASSERT_GE(expected_read_duration + DELAY_THRESHOLD, read_wall_time);

        ASSERT_LE((double)expected_write_duration - DELAY_THRESHOLD, total_write_delay);
        ASSERT_GE((double)expected_write_duration + DELAY_THRESHOLD, total_write_delay);
        ASSERT_LE((double)expected_read_duration - DELAY_THRESHOLD, total_read_delay);
        ASSERT_GE((double)expected_read_duration + DELAY_THRESHOLD, total_read_delay);
    }

    /**
     * @brief testing ssd utilization caused by occupied page
     * - execute page write
     * - verify ssd utilization equals \ssd_utils
     */
    TEST_P(SSDIoEmulatorUnitTest, SSDUtilCase1) {
        int flash_nb = 0;
        int block_nb = 0;
        int page_nb = 0;
        int occupied_pages = 1;
        double ssd_utils = (double)occupied_pages / PAGES_IN_SSD;

        SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, IO_PAGE_FIRST);

        usleep(MONITOR_SYNC_DELAY);
        ASSERT_EQ(ssd_utils, SSD_UTIL());
        ASSERT_EQ(ssd_utils, current_stats->utilization);
    }

    /**
     * @brief testing ssd utilization caused by occupied page by non-logical write
     * - execute non-logical page write
     * - verify ssd utilization equals \ssd_utils
     */
    TEST_P(SSDIoEmulatorUnitTest, SSDUtilCase2) {
        int flash_nb = 0;
        int block_nb = 0;
        int page_nb = 0;
        int occupied_pages = 1;
        double ssd_utils = (double)occupied_pages / PAGES_IN_SSD;

        SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, GC_WRITE, IO_PAGE_FIRST);

        usleep(MONITOR_SYNC_DELAY);
        ASSERT_EQ(ssd_utils, SSD_UTIL());
        ASSERT_EQ(ssd_utils, current_stats->utilization);
    }

    /**
     * @brief testing ssd utilization block erase affect
     * - execute page write on block \block_nb
     * - erase block \block_nb
     * - verify ssd utilization equals \ssd_utils
     */
    TEST_P(SSDIoEmulatorUnitTest, SSDUtilCase3) {
        int flash_nb = 0;
        int block_nb = 0;
        int page_nb = 0;
        int occupied_pages = 1-PAGE_NB;
        double ssd_utils = (double)occupied_pages / PAGES_IN_SSD;

        SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, IO_PAGE_FIRST);
        SSD_BLOCK_ERASE(flash_nb,block_nb);

        usleep(MONITOR_SYNC_DELAY);
        ASSERT_EQ(ssd_utils, SSD_UTIL());
        ASSERT_EQ(ssd_utils, current_stats->utilization);
    }

    /**
     * @brief testing affect of partial blocks erase no ssd utilization
     * - execute page write on block \block_nb
     * - execute page write on block \block_nb + 1
     * - verify ssd utilization match for 2 occupied pages
     * - erase block \block_nb
     * - verify ssd utilization match for 1 occupied pages: equals \ssd_utils
     */
    TEST_P(SSDIoEmulatorUnitTest, SSDUtilCase4) {

        int flash_nb = 0;
        int block_nb = 0;
        int page_nb = 0;
        int occupied_pages = 0;
        double ssd_utils = 0;

        SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, IO_PAGE_FIRST);
        SSD_PAGE_WRITE(flash_nb,block_nb+1,page_nb,0, WRITE, IO_PAGE_FIRST);
        occupied_pages = 2;
        ASSERT_EQ((double)occupied_pages / PAGES_IN_SSD, SSD_UTIL());
        SSD_BLOCK_ERASE(flash_nb,block_nb);
        occupied_pages-=PAGE_NB;

        ssd_utils = (double)occupied_pages / PAGES_IN_SSD;

        usleep(MONITOR_SYNC_DELAY);
        ASSERT_EQ(ssd_utils, SSD_UTIL());
        ASSERT_EQ(ssd_utils, current_stats->utilization);
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

        SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, IO_PAGE_FIRST);

        usleep(MONITOR_SYNC_DELAY);
        ASSERT_EQ(ssd.physical_page_writes, physical_page_writes);
        ASSERT_EQ(current_stats->write_count, physical_page_writes);

        ASSERT_EQ(ssd.logical_page_writes, logical_page_writes);
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

        int logical_page_writes = 0;
        int physical_page_writes = 1;

        SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, GC_WRITE, IO_PAGE_FIRST);

        usleep(MONITOR_SYNC_DELAY);
        ASSERT_EQ(physical_page_writes, ssd.physical_page_writes);
        ASSERT_EQ(physical_page_writes, current_stats->write_count);

        ASSERT_EQ(ssd.logical_page_writes, logical_page_writes);
        ASSERT_EQ(logical_write_count, logical_page_writes);
    }

} //namespace
