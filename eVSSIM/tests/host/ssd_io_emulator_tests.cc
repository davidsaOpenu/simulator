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

// default value for flash number
#define DEFAULT_FLASH_NB 4
#define IO_PAGE_NB 0
#define GC_IO_PAGE_NB -1

extern LogServer log_server;
extern int servSock;
extern int clientSock;
//logger_monitor log_monitor;
extern int errno;

extern ssd_disk ssd;
extern write_amplification_counters wa_counters;
extern RTLogStatistics *rt_log_stats;

// New browser delay values
extern int write_wall_time;
extern int read_wall_time;
//extern unsigned int logical_write_count;

// For logger_writer
extern logger_writer logger_writer_obj;

#define MONITOR_SYNC_DELAY_USEC 150000
#define DELAY_THRESHOLD 0

void MONITOR_SYNC_DELAY(int expected_duration) {
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
     * - execute 3 sequential page write
     * - verify Browser monitor total write bandwidth delay is \expected_write_duration
     */
    TEST_P(SSDIoEmulatorUnitTest, BWCaseWritesOnly1) {

        int flash_nb = 0;
        int block_nb = 0;
        int page_nb = 0;
        int offset = 0;
        int num_of_writes = 3;
        int write_wall_time = 0;

        SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,offset, WRITE);
        SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,offset, WRITE);
        SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,offset, WRITE);

        // single write page delay
        int expected_write_duration = (REG_WRITE_DELAY + CELL_PROGRAM_DELAY) * num_of_writes;

        // wait for new monitor sync
        MONITOR_SYNC_DELAY(expected_write_duration);

        uint32_t i;

        for (i = 0; i < FLASH_NB; i++) {
            write_wall_time += rt_log_stats[i].write_wall_time;
        }
        // new monitor write delay
        ASSERT_LE(write_wall_time - expected_write_duration, DELAY_THRESHOLD);
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
        int read_wall_time = 0;

        SSD_PAGE_READ(flash_nb,block_nb,page_nb,offset, READ);
        SSD_PAGE_READ(flash_nb,block_nb,page_nb,offset, READ);
        SSD_PAGE_READ(flash_nb,block_nb,page_nb,offset, READ);

        // single read page delay
        int expected_read_duration = CHANNEL_SWITCH_DELAY_R + (REG_READ_DELAY + CELL_READ_DELAY) * num_of_reads;

        // wait for new monitor sync
        MONITOR_SYNC_DELAY(expected_read_duration);

        uint32_t i;

        for (i = 0; i < FLASH_NB; i++) {
            read_wall_time += rt_log_stats[i].read_wall_time;
        }
        // new monitor read delay
        ASSERT_LE(read_wall_time - expected_read_duration, DELAY_THRESHOLD);
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
        int num_of_writes = 3;
        int num_of_reads = 2;
        int write_wall_time = 0;
        int read_wall_time = 0;

        SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,offset, WRITE);
        SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,offset, WRITE);
        SSD_PAGE_READ(flash_nb,block_nb,page_nb,offset, READ);
        SSD_PAGE_READ(flash_nb,block_nb,page_nb,offset, READ);
        SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,offset, WRITE);

        // single write page delay
        int expected_write_duration = CHANNEL_SWITCH_DELAY_W + (REG_WRITE_DELAY + CELL_PROGRAM_DELAY) * num_of_writes;
        int expected_read_duration = CHANNEL_SWITCH_DELAY_R + (REG_READ_DELAY + CELL_READ_DELAY) * num_of_reads;

        // wait for new monitor sync
        MONITOR_SYNC_DELAY(expected_write_duration + expected_read_duration);

        uint32_t i;

        for (i = 0; i < FLASH_NB; i++) {
            write_wall_time += rt_log_stats[i].write_wall_time;
            read_wall_time += rt_log_stats[i].read_wall_time;
        }
        // new monitor write delay
        ASSERT_LE(write_wall_time - expected_write_duration, DELAY_THRESHOLD);
        // new monitor read delay
        ASSERT_LE(abs(read_wall_time - expected_read_duration), DELAY_THRESHOLD);
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

        SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE);

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

        SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, GC_WRITE);

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
        int occupied_pages = 1-PAGE_NB;
        double ssd_utils = (double)occupied_pages / PAGES_IN_SSD;
        int expected_write_duration = REG_WRITE_DELAY + CELL_PROGRAM_DELAY + BLOCK_ERASE_DELAY;

        SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE);
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

        SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE);
        SSD_PAGE_WRITE(flash_nb,block_nb+1,page_nb,0, WRITE);
        occupied_pages = 2;
        ASSERT_EQ((double)occupied_pages / PAGES_IN_SSD, SSD_UTIL());
        SSD_BLOCK_ERASE(flash_nb,block_nb);
        occupied_pages-=PAGE_NB;

        ssd_utils = (double)occupied_pages / PAGES_IN_SSD;

        // wait for new monitor sync
        MONITOR_SYNC_DELAY(expected_write_duration);
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
        unsigned int logical_write_count = 0;
        int logical_page_writes = 1;
        int physical_page_writes = 1;
        int expected_write_duration = REG_WRITE_DELAY + CELL_PROGRAM_DELAY;

        SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE);
        // wait for new monitor sync
        MONITOR_SYNC_DELAY(expected_write_duration);

        uint32_t i;

        for (i = 0; i < FLASH_NB; i++) {
            logical_write_count += rt_log_stats[i].logical_write_count;
        }

        ASSERT_EQ(ssd.physical_page_writes, physical_page_writes);
        ASSERT_EQ(ssd.current_stats->write_count, physical_page_writes);

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
        unsigned int logical_write_count = 0;

        int logical_page_writes = 0;
        int physical_page_writes = 1;
        int expected_write_duration = REG_WRITE_DELAY + CELL_PROGRAM_DELAY;

        // wait for new monitor sync
        SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, GC_WRITE);

        MONITOR_SYNC_DELAY(expected_write_duration);

        uint32_t i;

        for (i = 0; i < FLASH_NB; i++) {
            logical_write_count += rt_log_stats[i].logical_write_count;
        }

        ASSERT_EQ(physical_page_writes, ssd.physical_page_writes);
        ASSERT_EQ(physical_page_writes, ssd.current_stats->write_count);

        ASSERT_EQ(ssd.logical_page_writes, logical_page_writes);
        ASSERT_EQ(logical_write_count, logical_page_writes);
    }

    /**
     * testing the statistics mechanism by issuing a full block write/read and validate them
     * - issue full block write
     * - issue full block read
     * - validate statistics
     */
    TEST_P(SSDIoEmulatorUnitTest, WriteRead1) {
        int flash_nb = 0;
        int block_nb = 0;
        int expected_write_duration = (REG_WRITE_DELAY + CELL_PROGRAM_DELAY) * CONST_PAGES_PER_BLOCK;
        int expected_read_duration = CHANNEL_SWITCH_DELAY_R + (REG_READ_DELAY + CELL_READ_DELAY) * CONST_PAGES_PER_BLOCK;
        int expected_rw = CONST_PAGES_PER_BLOCK;

        // Write all pages in the block
        for (size_t i = 0; i < CONST_PAGES_PER_BLOCK; i++) {
            SSD_PAGE_WRITE(flash_nb, block_nb, i, 0, WRITE);
        }

        // Read all pages in the block
        for (size_t i = 0; i < CONST_PAGES_PER_BLOCK; i++) {
            SSD_PAGE_READ(flash_nb, block_nb, i, 0, READ);
        }

        MONITOR_SYNC_DELAY(expected_write_duration + expected_read_duration);

        ASSERT_EQ(expected_rw, ssd.current_stats->write_count);
        ASSERT_EQ(expected_rw, ssd.current_stats->read_count);

    }

    /**
     * testing the statistics mechanism by issuing a full channel write/read and validate them
     * - issue full channel write
     * - issue full channel read
     * - validate statistics
     */
    TEST_P(SSDIoEmulatorUnitTest, WriteRead2) {
        std::pair<size_t,size_t> params = GetParam();
        size_t flash_num = params.second;
        size_t block_x_flash = pages_ / CONST_PAGES_PER_BLOCK;
        size_t blocks_per_flash = block_x_flash / flash_num;

        int expected_rw = CONST_PAGES_PER_BLOCK * blocks_per_flash;
        int expected_write_duration = (REG_WRITE_DELAY + CELL_PROGRAM_DELAY) * expected_rw;
        int expected_read_duration = CHANNEL_SWITCH_DELAY_R + (REG_READ_DELAY + CELL_READ_DELAY) * expected_rw;


        // Write all blocks in the channel
        for (size_t i = 0; i < blocks_per_flash; i++) {
            for (size_t j = 0; j < CONST_PAGES_PER_BLOCK; j++) {
                SSD_PAGE_WRITE(0, i, j, 0, WRITE);
            }
        }

        // Read all blocks in the channel
        for (size_t i = 0; i < blocks_per_flash; i++) {
            for (size_t j = 0; j < CONST_PAGES_PER_BLOCK; j++) {
                SSD_PAGE_READ(0, i, j, 0, READ);
            }
        }

        MONITOR_SYNC_DELAY(expected_write_duration + expected_read_duration);

        ASSERT_EQ(expected_rw, ssd.current_stats->write_count);
        ASSERT_EQ(expected_rw, ssd.current_stats->read_count);

    }

    /**
     * testing the write amplification calculation by writing over the whole flash twice
     * - write over flash twice using the FTL layer
     * - validate statistics
     */
    TEST_P(SSDIoEmulatorUnitTest, WriteAmplificationTest) {
        int expected_write_amplification = 1;

        // Write all flash
        for(int x=0; x<2; x++){
            for(size_t p=0; p < pages_; p++){
                ASSERT_EQ(FTL_SUCCESS, _FTL_WRITE_SECT(p * CONST_PAGE_SIZE_IN_BYTES, 1));
            }
        }

        // Assert w.a. is greater then 1
        ASSERT_LT(expected_write_amplification, ssd.current_stats->write_amplification);
    }

    // Dummy test for basic testing of logger_writer
    TEST_P(SSDIoEmulatorUnitTest, LoggerWriterPageWriteTest) {
        int flash_nb = 0;
        int block_nb = 0;
        int page_nb = 0;
        int offset = 0;
        //int expected_write_amplification = 1;
        int sz;
            int expected_write_duration = REG_WRITE_DELAY + CELL_PROGRAM_DELAY + 10000;

        //FILE *fp;

        SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,offset, WRITE);

        MONITOR_SYNC_DELAY(expected_write_duration);
//        fp = fopen("/tmp/log_file_0.log", "w");
//        fseek(fp, 0L, SEEK_END);
//        sz = ftell(fp);
//        fclose(fp);
        sz = logger_writer_obj.curr_size;

        ASSERT_NE(0, sz);
    }    

} //namespace
