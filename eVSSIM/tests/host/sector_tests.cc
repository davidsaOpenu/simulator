
extern "C" {

#include "common.h"
#include "common_setup.h"
#include "ftl_sect_strategy.h"
}
extern "C" int g_init;
extern "C" int clientSock;
extern "C" int g_init_log_server;
bool g_ci_mode = false;

#define GTEST_DONT_DEFINE_FAIL 1
#include <gtest/gtest.h>

#include <fstream>
#include <cstdio>
#include <cstdlib>

int main(int argc, char **argv) {
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--ci") == 0) {
            g_ci_mode = true;
        }
    }
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}


// TODO (https://trello.com/c/eRD6gano) create base class for simulator tests. Turn _server to be a private member of that
// class and have ObjectUnitTest inherit from the base class
pthread_t _server;

using namespace std;

namespace {



    class SectorUnitTest : public ::testing::TestWithParam<size_t> {
        public:
            //const static size_t CONST_BLOCK_NB_PER_FLASH = 4096;
            const static size_t CONST_FLASH_NUMBER = 4;
            const static size_t CONST_PAGES_PER_BLOCK = 8; // external (non over-provisioned)
            const static size_t CONST_PAGES_PER_BLOCK_OVERPROV = (CONST_PAGES_PER_BLOCK * 25) / 100; // 25 % of pages for over-provisioning
            const static size_t CONST_PAGE_SIZE_IN_BYTES = 4096;


            virtual void SetUp() {
                size_t mb = GetParam();
                pages_= mb * ((1024 * 1024) / CONST_PAGE_SIZE_IN_BYTES); // number_of_pages = disk_size (in MB) * 1048576 / page_size
                size_t block_x_flash = pages_ / CONST_PAGES_PER_BLOCK; // all_blocks_on_all_flashes = number_of_pages / pages_in_block
                //size_t flash = block_x_flash / CONST_BLOCK_NB_PER_FLASH; // number_of_flashes = all_blocks_on_all_flashes / number_of_blocks_in_flash
                size_t blocks_per_flash = block_x_flash / CONST_FLASH_NUMBER; // number_of_flashes = all_blocks_on_all_flashes / number_of_blocks_in_flash

                start_log_server();                

                ofstream ssd_conf("data/ssd.conf", ios_base::out | ios_base::trunc);
                ssd_conf << "FILE_NAME ./data/ssd.img\n"
                    "PAGE_SIZE " << CONST_PAGE_SIZE_IN_BYTES << "\n"
                    "PAGE_NB " << (CONST_PAGES_PER_BLOCK + CONST_PAGES_PER_BLOCK_OVERPROV) << "\n"
                    "SECTOR_SIZE 1\n"
                    "FLASH_NB " << CONST_FLASH_NUMBER << "\n"
                    "BLOCK_NB " << blocks_per_flash << "\n"
                    "PLANES_PER_FLASH 1\n"
                    "REG_WRITE_DELAY 82\n"
                    "CELL_PROGRAM_DELAY 900\n"
                    "REG_READ_DELAY 82\n"
                    "CELL_READ_DELAY 50\n"
                    "BLOCK_ERASE_DELAY 2000\n"
                    "CHANNEL_SWITCH_DELAY_R 16\n"
                    "CHANNEL_SWITCH_DELAY_W 33\n"
                    "CHANNEL_NB " << CONST_FLASH_NUMBER << "\n"
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
                clientSock = 0;
                g_init_log_server = 0;
            }
        protected:
            size_t pages_;
    }; // OccupySpaceStressTest

    std::vector<size_t> GetParams() {
        std::vector<size_t> list;


        if (g_ci_mode) {
            printf("Running in CI mode\n");
            list.push_back(1);
            list.push_back(2);
            list.push_back(4);
            list.push_back(8);

            return list;
        }

        list.push_back(256);
        return list;
    }

    INSTANTIATE_TEST_CASE_P(DiskSize, SectorUnitTest, ::testing::ValuesIn(GetParams()));
    TEST_P(SectorUnitTest, SequentialOnePageAtTimeWrite) {
        for(int x=0; x<2 /*8*/; x++){
            for(size_t p=0; p < pages_; p++){
                //std::cout << "hello"  << p << "\n";
                ASSERT_EQ(FTL_SUCCESS, _FTL_WRITE_SECT(p * CONST_PAGE_SIZE_IN_BYTES, 1));
            }
        }
    }

    TEST_P(SectorUnitTest, RandomOnePageAtTimeWrite) {
        for(int x=0; x<2 /*8*/; x++){
            for(size_t p=0; p < pages_; p++){
                ASSERT_EQ(FTL_SUCCESS, _FTL_WRITE_SECT((rand() % pages_) * CONST_PAGE_SIZE_IN_BYTES, 1));
            }
        }
    }

    TEST_P(SectorUnitTest, MixSequentialAndRandomOnePageAtTimeWrite) {
        for(int x=0; x<2; x++){
            for(size_t p=0; p < pages_; p++){
                ASSERT_EQ(FTL_SUCCESS, _FTL_WRITE_SECT((rand() % pages_) * CONST_PAGE_SIZE_IN_BYTES, 1));
            }
            for(size_t p=0; p < pages_; p++){
                ASSERT_EQ(FTL_SUCCESS, _FTL_WRITE_SECT(p * CONST_PAGE_SIZE_IN_BYTES, 1));
            }
        }
    }
} //namespace
