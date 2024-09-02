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

#ifndef BASE_EMULATOR_TESTS_H
#define BASE_EMULATOR_TESTS_H

extern "C" {

#include "common.h"
#include "ftl_sect_strategy.h"
}
extern "C" int g_init;
extern "C" int clientSock;
extern "C" int g_init_log_server;

#define GTEST_DONT_DEFINE_FAIL 1
#include <gtest/gtest.h>

#include <fstream>
#include <cstdio>
#include <cstdlib>


#define BASE_TEST_ARRAY_SIZE(array) (sizeof(array) / sizeof(array[0]))

/** default value for flash number */
#define DEFAULT_FLASH_NB 8

/**
 * paramters that the tests run with
 * @param sizemb - disk size parameter
 *  testing disk sizes of 1,2,4 and 8 megabyte
 * @param flashnb - number of flash memories
 *  testing flash numbers 2,4,8,16 and 32
 * @param objsize - object size parameter
 *  testing object sizes of 2048 and 4098 megabytes
 */
namespace parameters
{
    enum sizemb
    {
        mb1 = 1,
        mb2 = 2,
        mb3 = 4,
        mb4 = 8
    };

    static const sizemb Allsizemb[] = { mb1, mb2, mb3, mb4 };

    enum flashnb
    {
        fnb1 = 2,
        fnb2 = 4,
        fnb3 = 8,
        fnb4 = 16,
        fnb5 = 32
    };

    static const flashnb Allflashnb[] = { fnb1, fnb2, fnb3, fnb4, fnb5};

        enum objsize
    {
        os1 = 2048,
        os2 = 4098
    };

    static const objsize Allobjsize[] = { os1, os2 };
}

using namespace std;

namespace {
    class SSDConf {
    private:
        size_t page_size;
        size_t page_nb;
        size_t sector_size;
        size_t flash_nb;
        size_t block_nb;
        size_t channel_nb;
        size_t logger_size;
        size_t object_size;
        size_t pages;

        // external blocks (non over-provisioned)
        const static size_t CONST_PAGES_PER_BLOCK = 8;
        // 25 % of pages for over-provisioning
        const static size_t CONST_PAGES_PER_BLOCK_OVERPROV = (CONST_PAGES_PER_BLOCK * 25) / 100;
        const static size_t CONST_PAGE_SIZE_IN_BYTES = 4096;

        void ssd_conf_calc_based_size_mb(size_t size_mb) {
            // number_of_pages = disk_size (in MB) * 1048576 / page_size
            this->pages = size_mb * ((1024 * 1024) / CONST_PAGE_SIZE_IN_BYTES);
            // all_blocks_on_all_flashes = number_of_pages / pages_in_block
            size_t block_x_flash = this->pages / CONST_PAGES_PER_BLOCK;
            // number_of_flashes = all_blocks_on_all_flashes / number_of_blocks_in_flash
            size_t blocks_per_flash = block_x_flash / DEFAULT_FLASH_NB;
            this->block_nb = blocks_per_flash;
        }

    public:
        SSDConf(size_t size_mb) {
            ssd_conf_calc_based_size_mb(size_mb);
            this->page_size = CONST_PAGE_SIZE_IN_BYTES;
            this->page_nb = CONST_PAGES_PER_BLOCK + CONST_PAGES_PER_BLOCK_OVERPROV;
            this->flash_nb = DEFAULT_FLASH_NB;
            this->channel_nb = DEFAULT_FLASH_NB;
            this->sector_size = 1;
            this->object_size = 2048; // megabytes
        }

        SSDConf(size_t page_size, size_t page_nb, size_t sector_size,
                size_t flash_nb, size_t block_nb, size_t channel_nb)
                : page_size(page_size), page_nb(page_nb), sector_size(sector_size),
                  flash_nb(flash_nb), block_nb(block_nb), channel_nb(channel_nb) {
				  this->pages = page_nb * block_nb * flash_nb;
                  }

        size_t get_page_size(void) {
            return this->page_size;
        }

        size_t get_page_nb(void) {
            return this->page_nb;
        }

        size_t get_sector_size(void) {
            return this->sector_size;
        }

        size_t get_flash_nb(void) {
            return this->flash_nb;
        }

        size_t get_block_nb(void) {
            return this->block_nb;
        }

        size_t get_channel_nb(void) {
            return this->channel_nb;
        }

        size_t get_logger_size(void) {
            return this->logger_size;
        }

        size_t get_object_size(void) {
            return this->object_size;
        }

        size_t get_pages(void) {
            return this->pages;
        }

        size_t get_pages_per_block(void) {
            return CONST_PAGES_PER_BLOCK;
        }

        void set_logger_size(size_t val) {
            this->logger_size = val;
        }

        void set_object_size(size_t val) {
            this->object_size = val;
        }

        void ssd_conf_serialize(void) {
            ofstream ssd_conf("data/ssd.conf", ios_base::out | ios_base::trunc);
            ssd_conf << "FILE_NAME ./data/ssd.img\n"
                "PAGE_SIZE " << get_page_size() << "\n"
                "PAGE_NB " << get_page_nb() << "\n"
                "SECTOR_SIZE " << get_sector_size() << "\n"
                "FLASH_NB " << get_flash_nb() << "\n"
                "BLOCK_NB " << get_block_nb() << "\n"
                "PLANES_PER_FLASH 1\n"
                "REG_WRITE_DELAY 82\n"
                "CELL_PROGRAM_DELAY 900\n"
                "REG_READ_DELAY 82\n"
                "CELL_READ_DELAY 50\n"
                "BLOCK_ERASE_DELAY 2000\n"
                "CHANNEL_SWITCH_DELAY_R 16\n"
                "CHANNEL_SWITCH_DELAY_W 33\n"
                "CHANNEL_NB " << get_channel_nb() << "\n"
                "STAT_TYPE 15\n"
                "STAT_SCOPE 62\n"
                "STAT_PATH /tmp/stat.csv\n"
                "STORAGE_STRATEGY 1\n"; // sector strategy
            ssd_conf.close();
        }
    };

    /* override due to valgrind error and gtest using this operator<< */
    ostream& operator<<(ostream& os, const SSDConf& value) {
        (void) value;
        return os;
    }

    class BaseTest : public ::testing::TestWithParam<SSDConf*> {
        private:
            SSDConf* ssd_config;

        public:
            virtual void SetUp(void) {
                ssd_config = GetParam();
                ssd_config->ssd_conf_serialize();
                FTL_INIT();
            }

            virtual void TearDown() {
                FTL_TERM();
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
                delete ssd_config;
            }

            SSDConf* base_test_get_ssd_config(void) {
                return this->ssd_config;
            }
    }; // OccupySpaceStressTest

} //namespace

#endif
