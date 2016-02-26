
extern "C" {
#include "common.h"
#include "ftl_sect_strategy.h"
}
extern "C" int g_init;
extern "C" int g_server_create;
extern "C" int g_init_log_server;

#define GTEST_DONT_DEFINE_FAIL 1
#include <gtest/gtest.h>

#include <fstream>
#include <cstdio>
#include <cstdlib>

using namespace std;

namespace {

    class SectorUnitTest : public ::testing::TestWithParam<size_t> {
        public:
            virtual void SetUp() {
                size_t mb = GetParam();
                pages_= (mb * 1024 * 1024) / 4096; // number_of_pages = disk_size (in bytes) / page_size in bytes
                size_t block_x_flash = pages_ / 8; // all_blocks_on_all_flashes = number_of_pages / pages_in_block
                size_t flash = block_x_flash / 4096; // number_of_flashes = all_blocks_on_all_flashes / number_of_blocks_in_flash
                ofstream ssd_conf("data/ssd.conf", ios_base::out | ios_base::trunc);
                ssd_conf << "FILE_NAME ./data/ssd.img\n"
                    "PAGE_SIZE 4096\n" // page size in bytes
                    "PAGE_NB 10\n" // 8 pages per block +2 pages over-provision = 125% of disk size
                    "SECTOR_SIZE 1\n"
                    "FLASH_NB 32\n"   // << flash << "\n" // see calculations above
                    "BLOCK_NB 4096\n" // amount of blocks per flash
                    "PLANES_PER_FLASH 1\n"
                    "REG_WRITE_DELAY 82\n"
                    "CELL_PROGRAM_DELAY 900\n"
                    "REG_READ_DELAY 82\n"
                    "CELL_READ_DELAY 50\n"
                    "BLOCK_ERASE_DELAY 2000\n"
                    "CHANNEL_SWITCH_DELAY_R 16\n"
                    "CHANNEL_SWITCH_DELAY_W 33\n"
                    "CHANNEL_NB 4\n"
                    "STAT_TYPE 15\n"
                    "STAT_SCOPE 62\n"
                    "STAT_PATH /tmp/stat.csv\n"
                    "STORAGE_STRATEGY 1\n"; // sector strategy
                ssd_conf.close();
            	FTL_INIT();
            #ifdef MONITOR_ON
            	INIT_LOG_MANAGER();
            #endif
            }
            virtual void TearDown() {
            	FTL_TERM();
            #ifdef MONITOR_ON
            	TERM_LOG_MANAGER();
            #endif
                remove("data/empty_block_list.dat");
                remove("data/inverse_block_mapping.dat");
                remove("data/inverse_page_mapping.dat");
                remove("data/mapping_table.dat");
                remove("data/valid_array.dat");
                remove("data/victim_block_list.dat");
                remove("data/ssd.conf");
                g_init = 0;
                g_server_create = 0;
                g_init_log_server = 0;
            }
        protected:
            size_t pages_;
    }; // OccupySpaceStressTest

    INSTANTIATE_TEST_CASE_P(DiskSize, SectorUnitTest, ::testing::Values(512 /*MB*/, 1024 /*1G*/, 4096 /*4G*/)); //Values are in MB

    TEST_P(SectorUnitTest, SequentialOnePageAtTimeWrite) {
        for(int x=0; x<8; x++){
            for(size_t p=0; p < pages_; p++){
                ASSERT_EQ(SUCCESSFUL, _FTL_WRITE_SECT(p * 4096, 1));
            }
        }
    }

    TEST_P(SectorUnitTest, RandomOnePageAtTimeWrite) {
        for(int x=0; x<8; x++){
            for(size_t p=0; p < pages_; p++){
                ASSERT_EQ(SUCCESSFUL, _FTL_WRITE_SECT((rand() % pages_) * 4096, 1));
            }
        }
    }

    TEST_P(SectorUnitTest, MixSequentialAndRandomOnePageAtTimeWrite) {
        for(int x=0; x<2; x++){
            for(size_t p=0; p < pages_; p++){
                ASSERT_EQ(SUCCESSFUL, _FTL_WRITE_SECT((rand() % pages_) * 4096, 1));
            }
            for(size_t p=0; p < pages_; p++){
                ASSERT_EQ(SUCCESSFUL, _FTL_WRITE_SECT(p * 4096, 1));
            }
        }
    }
} //namespace
