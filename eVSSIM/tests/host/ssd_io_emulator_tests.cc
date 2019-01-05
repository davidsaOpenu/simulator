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

#include "ssd_log_monitor.h"

// default value for flash number
#define DEFAULT_FLASH_NB 4

extern LogServer log_server;
extern int servSock;
extern int clientSock;
logger_monitor log_monitor;
extern int errno;

#define BW_DELAY_THRESHOLD 8

#define THREADS_START_DELAY 100000


// busy wait until @timestamp
//void _SSD_BUSY_WAIT(int64_t* until_timestamp, int op_nb) {
//	int64_t now = get_usec();
//	while( now < *until_timestamp ){
//		now = get_usec();
//	}
//}
//

int bw_delay_threshold(int operations_count) {
	return BW_DELAY_THRESHOLD * operations_count;
}

typedef struct {
	int64_t timestamp;
	int64_t* started_timestamp;
	unsigned int flash_nb;
	unsigned int block_nb;
	unsigned int page_nb;
    int offset;
    int type;
    int io_page_nb;
} ssd_args_struct;

void* ssd_write_thread(void* args)
{
	ssd_args_struct* args_struct = (ssd_args_struct*)args;
	_SSD_BUSY_WAIT(args_struct->timestamp);
	*(args_struct->started_timestamp) = get_usec();
	SSD_PAGE_WRITE(args_struct->flash_nb
			, args_struct->block_nb
			, args_struct->page_nb
			, args_struct->offset
			, args_struct->type
			, args_struct->io_page_nb);
	return 0;
}


void* ssd_read_thread(void* args)
{
	ssd_args_struct* args_struct = (ssd_args_struct*)args;
	_SSD_BUSY_WAIT(args_struct->timestamp);
	*(args_struct->started_timestamp) = get_usec();
	SSD_PAGE_READ(args_struct->flash_nb
			, args_struct->block_nb
			, args_struct->page_nb
			, args_struct->offset
			, args_struct->type
			, args_struct->io_page_nb);
	return 0;
}


void* ssd_erase_thread(void* args)
{
	ssd_args_struct* args_struct = (ssd_args_struct*)args;
	_SSD_BUSY_WAIT(args_struct->timestamp);
	*(args_struct->started_timestamp) = get_usec();
	SSD_BLOCK_ERASE(args_struct->flash_nb
			, args_struct->block_nb);
	return 0;
}


pthread_t* run_ssd_page_write_thread(int64_t timestamp, int64_t* started
		, unsigned int flash_nb
		, unsigned int block_nb
		, unsigned int page_nb
		, int offset
		, int type
		, int io_page_nb) {

    pthread_t* th = (pthread_t *) malloc(sizeof(pthread_t));

    ssd_args_struct* args = (ssd_args_struct*)malloc(sizeof(ssd_args_struct));
    args->timestamp = timestamp;
    args->started_timestamp = started;
    args->flash_nb = flash_nb;
    args->block_nb = block_nb;
    args->page_nb = page_nb;
    args->offset = offset;
    args->type = type;
    args->io_page_nb = io_page_nb;
    pthread_create(th, NULL, &ssd_write_thread, (void *)args);
    return th;
}


pthread_t* run_ssd_page_read_thread(int64_t timestamp, int64_t* started
		, unsigned int flash_nb
		, unsigned int block_nb
		, unsigned int page_nb
		, int offset
		, int type
		, int io_page_nb) {

    pthread_t* th = (pthread_t *) malloc(sizeof(pthread_t));

    ssd_args_struct* args = (ssd_args_struct*)malloc(sizeof(ssd_args_struct));
    args->timestamp = timestamp;
    args->started_timestamp = started;
    args->flash_nb = flash_nb;
    args->block_nb = block_nb;
    args->page_nb = page_nb;
    args->offset = offset;
    args->type = type;
    args->io_page_nb = io_page_nb;

	int64_t t = get_usec();
    pthread_create(th, NULL, &ssd_read_thread, (void *)args);
    return th;
}


pthread_t* run_ssd_block_erase_thread(int64_t timestamp, int64_t* started
		, unsigned int flash_nb
		, unsigned int block_nb) {

    pthread_t* th = (pthread_t *) malloc(sizeof(pthread_t));

    ssd_args_struct* args = (ssd_args_struct*)malloc(sizeof(ssd_args_struct));
    args->timestamp = timestamp;
    args->started_timestamp = started;
    args->flash_nb = flash_nb;
    args->block_nb = block_nb;
    pthread_create(th, NULL, &ssd_erase_thread, (void *)args);
    return th;
}

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
	 * !brief testing delay caused by single page write
	 * -# execute page write
	 * -# verify total write bandwidth delay is register write + cell program delays
	 */
	TEST_P(SSDIoEmulatorUnitTest, BWCase1) {

		int flash_nb = 0;
		int block_nb = 0;
		int page_nb = 0;

		SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, IO_PAGE_FIRST);
		int write_count = 1;

		unsigned long int expected_duration;
		//
		// duration calculation:
		//
		// time=0
		//		1st register write start
		expected_duration = 0;
		// time=@REG_WRITE_DELAY
		//		1st register write done
		expected_duration += REG_WRITE_DELAY;
		// time=@REG_WRITE_DELAY + @CELL_PROGRAM_DELAY
		//		1st cell programming done
		expected_duration += CELL_PROGRAM_DELAY;

		ASSERT_GE(GET_IO_BANDWIDTH((double)expected_duration / write_count), SSD_WRITE_BANDWIDTH());
		ASSERT_LE(GET_IO_BANDWIDTH((double)(expected_duration + bw_delay_threshold(write_count)) / write_count)
				, SSD_WRITE_BANDWIDTH());
	}

	/**
	 * !brief testing read bandwidth of single page read
	 * -# execute page write
	 * -# execute page read
	 * -# verify total read bandwidth delay is sum of
	 * 		channel switch read + register write + cell program delays
	 */
	TEST_P(SSDIoEmulatorUnitTest, BWCase2) {

		int flash_nb = 0;
		int block_nb = 0;
		int page_nb = 0;

		unsigned long int expected_write_duration, expected_read_duration;
		int write_ops = 1;
		int read_ops = 1;

		SSD_PAGE_WRITE(flash_nb,block_nb,block_nb,0, WRITE, IO_PAGE_FIRST);
		SSD_PAGE_READ(flash_nb,block_nb,block_nb,0, READ, IO_PAGE_FIRST);	// cause switch read delay

		//
		// write duration calculation:
		//
		// time=0
		//		1st register write start
		expected_write_duration = 0;
		expected_read_duration = 0;
		// time=@REG_WRITE_DELAY
		//		1st register write done
		expected_write_duration += REG_WRITE_DELAY;
		//		1st cell programming start
		// time+= @CELL_PROGRAM_DELAY
		//		1st cell programming done
		expected_write_duration += CELL_PROGRAM_DELAY;
		//		1st read channel switch start
		// time+= @CHANNEL_SWITCH_DELAY_W
		//		1st read channel switch done
		expected_read_duration += CHANNEL_SWITCH_DELAY_R;
		//		1st register read start
		// time+=@REG_READ_DELAY
		//		1st register read done
		expected_read_duration += REG_READ_DELAY;
		//		1st read programming start
		// time+=@CELL_READ_DELAY
		//		1st read programming done
		expected_read_duration += CELL_READ_DELAY;

		ASSERT_LE(expected_write_duration, SSD_TOTAL_WRITE_DELAY());
		ASSERT_GE(expected_write_duration + bw_delay_threshold(write_ops), SSD_TOTAL_WRITE_DELAY());

		ASSERT_LE(expected_read_duration, SSD_TOTAL_READ_DELAY());
		ASSERT_GE(expected_read_duration + bw_delay_threshold(read_ops), SSD_TOTAL_READ_DELAY());
	}

    // case 3: gc read after write
	/**
	 * !brief testing write bandwidth of single gc page read
	 * -# execute page write
	 * -# execute gc page read
	 * -# verify total write bandwidth delay include
	 * 		page write delays: channel switch read + register write + cell program delays
	 */
	TEST_P(SSDIoEmulatorUnitTest, BWCase3) {

		int flash_nb = 0;
		int block_nb = 0;
		int page_nb = 0;

		unsigned long int expected_write_duration, expected_read_duration;
		int write_ops = 2;
		int read_ops = 0;

		SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, IO_PAGE_FIRST);
		SSD_PAGE_READ(flash_nb,block_nb,page_nb,0, GC_READ, IO_PAGE_FIRST);	// cause switch read delay

		//
		// write duration calculation:
		//
		// time=0
		//		1st register write start
		expected_write_duration = 0;
		expected_read_duration = 0;
		// time=@REG_WRITE_DELAY
		//		1st register write done
		expected_write_duration += REG_WRITE_DELAY;
		//		1st cell programming start
		// time+= @CELL_PROGRAM_DELAY
		//		1st cell programming done
		expected_write_duration += CELL_PROGRAM_DELAY;
		//		1st read channel switch start
		// time+= @CHANNEL_SWITCH_DELAY_W
		//		1st read channel switch done
		expected_write_duration += CHANNEL_SWITCH_DELAY_R;
		//		1st register read start
		// time+=@REG_READ_DELAY
		//		1st register read done
		expected_write_duration += REG_READ_DELAY;
		//		1st read programming start
		// time+=@CELL_READ_DELAY
		//		1st read programming done
		expected_write_duration += CELL_READ_DELAY;

		ASSERT_LE(expected_write_duration, SSD_TOTAL_WRITE_DELAY());
		ASSERT_GE(expected_write_duration + bw_delay_threshold(write_ops), SSD_TOTAL_WRITE_DELAY());

		ASSERT_LE(expected_read_duration, SSD_TOTAL_READ_DELAY());
		ASSERT_GE(expected_read_duration + bw_delay_threshold(read_ops), SSD_TOTAL_READ_DELAY());
	}

	// case 4 erase after write
	/**
	 * !
	 * /brief
	 * Write / Read bandwidth
	 * on sequential page write / read switching
	 */
	TEST_P(SSDIoEmulatorUnitTest, BWCase4) {

		int flash_nb = 0;
		int block_nb = 0;
		int page_nb = 0;

		unsigned long int expected_write_duration, expected_read_duration;
		int executed_operations = 2;

		BLOCK_ERASE_DELAY = 10;

		SSD_PAGE_WRITE(flash_nb,block_nb,0,0, WRITE, IO_PAGE_FIRST);
		SSD_BLOCK_ERASE(flash_nb,block_nb);

		//
		// write duration calculation:
		//
		// time=0
		//		1st register write start
		expected_write_duration = 0;
//		expected_read_duration = 0;
		// time=@REG_WRITE_DELAY
		//		1st register write done
		expected_write_duration += REG_WRITE_DELAY;
		//		1st cell programming start
		// time+= @CELL_PROGRAM_DELAY
		//		1st cell programming done
		expected_write_duration += CELL_PROGRAM_DELAY;
		//		1st read channel switch start
		// time+= @BLOCK_ERASE_DELAY
		//		1st read channel switch done
		expected_write_duration += BLOCK_ERASE_DELAY;

		ASSERT_LE(expected_write_duration, SSD_TOTAL_WRITE_DELAY());
		ASSERT_GE(expected_write_duration + bw_delay_threshold(executed_operations), SSD_TOTAL_WRITE_DELAY());
	}



	// case 5 write after write
	/**
	 * !
	 * /brief
	 * Write / Read bandwidth
	 * on sequential page write / read switching
	 */
	TEST_P(SSDIoEmulatorUnitTest, BWCase5) {

		int flash_nb = 0;
		int block_nb = 0;
		int page_nb = 0;

		unsigned long int expected_write_duration, expected_read_duration;
		int executed_operations = 2;

		BLOCK_ERASE_DELAY = 10;

		SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, IO_PAGE_FIRST);
		SSD_PAGE_WRITE(flash_nb,block_nb+1,page_nb,0, WRITE, IO_PAGE_FIRST);

		//
		// write duration calculation:
		//
		// time=0
		//		1st register write start
		expected_write_duration = 0;
//		expected_read_duration = 0;
		// time=@REG_WRITE_DELAY
		//		1st register write done
		expected_write_duration += REG_WRITE_DELAY;
		//		1st cell programming start
		// time+= @CELL_PROGRAM_DELAY
		//		1st cell programming done
		expected_write_duration += CELL_PROGRAM_DELAY;
		//		1st read channel switch start
		// time+= @BLOCK_ERASE_DELAY
		//		1st read channel switch done
		expected_write_duration += REG_WRITE_DELAY;
		expected_write_duration += CELL_PROGRAM_DELAY;

		ASSERT_LE(expected_write_duration, SSD_TOTAL_WRITE_DELAY());
		ASSERT_GE(expected_write_duration + bw_delay_threshold(executed_operations), SSD_TOTAL_WRITE_DELAY());
	}



	// case 6 read after write & write after read
	/**
	 * !
	 * /brief
	 * Write / Read bandwidth
	 * on sequential page write / read switching
	 */
	TEST_P(SSDIoEmulatorUnitTest, BWCase6) {

		int flash_nb = 0;
		int block_nb = 0;
		int page_nb = 0;

		unsigned long int expected_write_duration, expected_read_duration;
		int write_ops = 2;
		int read_ops = 1;

		SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, IO_PAGE_FIRST);
		SSD_PAGE_READ(flash_nb,block_nb,page_nb,0, READ, IO_PAGE_FIRST);	// cause switch read delay
		SSD_PAGE_WRITE(flash_nb,block_nb+1,page_nb,0, WRITE, IO_PAGE_FIRST);

		//
		// write duration calculation:
		//
		// time=0
		//		1st register write start
		expected_write_duration = 0;
		expected_read_duration = 0;
		// time=@REG_WRITE_DELAY
		//		1st register write done
		expected_write_duration += REG_WRITE_DELAY;
		//		1st cell programming start
		// time+= @CELL_PROGRAM_DELAY
		//		1st cell programming done
		expected_write_duration += CELL_PROGRAM_DELAY;
		//		1st read channel switch start
		// time+= @CHANNEL_SWITCH_DELAY_W
		//		1st read channel switch done
		expected_read_duration += CHANNEL_SWITCH_DELAY_R;
		//		1st register read start
		// time+=@REG_READ_DELAY
		//		1st register read done
		expected_read_duration += REG_READ_DELAY;
		//		1st read programming start
		// time+=@CELL_READ_DELAY
		//		1st read programming done
		expected_read_duration += CELL_READ_DELAY;

		expected_write_duration += CHANNEL_SWITCH_DELAY_W;
		expected_write_duration += REG_WRITE_DELAY;
		expected_write_duration += CELL_PROGRAM_DELAY;


		ASSERT_LE(expected_write_duration, SSD_TOTAL_WRITE_DELAY());
		ASSERT_GE(expected_write_duration + bw_delay_threshold(write_ops), SSD_TOTAL_WRITE_DELAY());

		ASSERT_LE(expected_read_duration, SSD_TOTAL_READ_DELAY());
		ASSERT_GE(expected_read_duration + bw_delay_threshold(read_ops), SSD_TOTAL_READ_DELAY());
	}



	// case 7 gc read after write & write after gc read
	/**
	 * !
	 * /brief
	 * Write / Read bandwidth
	 * on sequential page write / read switching
	 */
	TEST_P(SSDIoEmulatorUnitTest, BWCase7) {

		int flash_nb = 0;
		int block_nb = 0;
		int page_nb = 0;

		unsigned long int expected_write_duration, expected_read_duration;
		int write_ops = 3;
		int read_ops = 0;

		SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, IO_PAGE_FIRST);
		SSD_PAGE_READ(flash_nb,block_nb,page_nb,0, GC_READ, IO_PAGE_FIRST);	// cause switch read delay
		SSD_PAGE_WRITE(flash_nb,block_nb+1,page_nb,0, WRITE, IO_PAGE_FIRST);

		//
		// write duration calculation:
		//
		// time=0
		//		1st register write start
		expected_write_duration = 0;
		expected_read_duration = 0;
		// time=@REG_WRITE_DELAY
		//		1st register write done
		expected_write_duration += REG_WRITE_DELAY;
		//		1st cell programming start
		// time+= @CELL_PROGRAM_DELAY
		//		1st cell programming done
		expected_write_duration += CELL_PROGRAM_DELAY;
		//		1st read channel switch start
		// time+= @CHANNEL_SWITCH_DELAY_W
		//		1st read channel switch done
		expected_write_duration += CHANNEL_SWITCH_DELAY_R;
		//		1st register read start
		// time+=@REG_READ_DELAY
		//		1st register read done
		expected_write_duration += REG_READ_DELAY;
		//		1st read programming start
		// time+=@CELL_READ_DELAY
		//		1st read programming done
		expected_write_duration += CELL_READ_DELAY;

		expected_write_duration += CHANNEL_SWITCH_DELAY_W;
		expected_write_duration += REG_WRITE_DELAY;
		expected_write_duration += CELL_PROGRAM_DELAY;


		ASSERT_LE(expected_write_duration, SSD_TOTAL_WRITE_DELAY());
		ASSERT_GE(expected_write_duration + bw_delay_threshold(write_ops), SSD_TOTAL_WRITE_DELAY());

		ASSERT_LE(expected_read_duration, SSD_TOTAL_READ_DELAY());
		ASSERT_GE(expected_read_duration + bw_delay_threshold(read_ops), SSD_TOTAL_READ_DELAY());
	}



	// case 8 read after erase
	/**
	 * !
	 * /brief
	 * Write / Read bandwidth
	 * on sequential write erase read
	 */
	TEST_P(SSDIoEmulatorUnitTest, BWCase8) {

		int flash_nb = 0;
		int block_nb = 0;
		int page_nb = 0;

		unsigned long int expected_write_duration, expected_read_duration;
		int write_ops = 2;
		int read_ops = 1;

		SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, IO_PAGE_FIRST);
		SSD_BLOCK_ERASE(flash_nb,block_nb+1);
		SSD_PAGE_READ(flash_nb,block_nb,page_nb,0, READ, IO_PAGE_FIRST);	// cause switch read delay

		//
		// write duration calculation:
		//
		// time=0
		//		1st register write start
		expected_write_duration = 0;
		expected_read_duration = 0;
		// time=@REG_WRITE_DELAY
		//		1st register write done
		expected_write_duration += REG_WRITE_DELAY;
		//		1st cell programming start
		// time+= @CELL_PROGRAM_DELAY
		//		1st cell programming done
		expected_write_duration += CELL_PROGRAM_DELAY;
		//		1st read channel switch start
		// time+= @CHANNEL_SWITCH_DELAY_W
		//		1st read channel switch done
		expected_write_duration += BLOCK_ERASE_DELAY;

		expected_read_duration += CHANNEL_SWITCH_DELAY_R;
		//		1st register read start
		// time+=@REG_READ_DELAY
		//		1st register read done
		expected_read_duration += REG_READ_DELAY;
		//		1st read programming start
		// time+=@CELL_READ_DELAY
		//		1st read programming done
		expected_read_duration += CELL_READ_DELAY;

		ASSERT_LE(expected_write_duration, SSD_TOTAL_WRITE_DELAY());
		ASSERT_GE(expected_write_duration + bw_delay_threshold(write_ops), SSD_TOTAL_WRITE_DELAY());

		ASSERT_LE(expected_read_duration, SSD_TOTAL_READ_DELAY());
		ASSERT_GE(expected_read_duration + bw_delay_threshold(read_ops), SSD_TOTAL_READ_DELAY());
	}


	// case 9 write after erase
	/**
	 * !
	 * /brief
	 * Write / Read bandwidth
	 * on sequential write erase read
	 */
	TEST_P(SSDIoEmulatorUnitTest, BWCase9) {

		int flash_nb = 0;
		int block_nb = 0;
		int page_nb = 0;

		unsigned long int expected_write_duration, expected_read_duration;
		int write_ops = 3;
		int read_ops = 0;


		SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, IO_PAGE_FIRST);
		SSD_BLOCK_ERASE(flash_nb,block_nb);
		SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, IO_PAGE_FIRST);

		//
		// write duration calculation:
		//
		// time=0
		//		1st register write start
		expected_write_duration = 0;
		expected_read_duration = 0;
		// time=@REG_WRITE_DELAY
		//		1st register write done
		expected_write_duration += REG_WRITE_DELAY;
		//		1st cell programming start
		// time+= @CELL_PROGRAM_DELAY
		//		1st cell programming done
		expected_write_duration += CELL_PROGRAM_DELAY;
		//		1st read channel switch start
		// time+= @CHANNEL_SWITCH_DELAY_W
		//		1st read channel switch done
		expected_write_duration += BLOCK_ERASE_DELAY;

		//		1st register read start
		// time+=@REG_READ_DELAY
		//		1st register read done
		expected_write_duration += REG_WRITE_DELAY;
		//		1st read programming start
		// time+=@CELL_READ_DELAY
		//		1st read programming done
		expected_write_duration += CELL_PROGRAM_DELAY;

		ASSERT_LE(expected_write_duration, SSD_TOTAL_WRITE_DELAY());
		ASSERT_GE(expected_write_duration + bw_delay_threshold(write_ops), SSD_TOTAL_WRITE_DELAY());

		ASSERT_LE(expected_read_duration, SSD_TOTAL_READ_DELAY());
		ASSERT_GE(expected_read_duration + bw_delay_threshold(read_ops), SSD_TOTAL_READ_DELAY());
	}


	// case 10 read after read
	/**
	 * !
	 * /brief
	 * Write / Read bandwidth
	 * on sequential write erase read
	 */
	TEST_P(SSDIoEmulatorUnitTest, BWCase10) {

		int flash_nb = 0;
		int block_nb = 0;
		int page_nb = 0;

		unsigned long int expected_write_duration, expected_read_duration;
		int write_ops = 1;
		int read_ops = 2;

		SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, IO_PAGE_FIRST);
		SSD_PAGE_READ(flash_nb,block_nb,page_nb,0, READ, IO_PAGE_FIRST);
		SSD_PAGE_READ(flash_nb,block_nb,page_nb,0, READ, IO_PAGE_FIRST);

		//
		// write duration calculation:
		//
		// time=0
		//		1st register write start
		expected_write_duration = 0;
		expected_read_duration = 0;
		// time=@REG_WRITE_DELAY
		//		1st register write done
		expected_write_duration += REG_WRITE_DELAY;
		//		1st cell programming start
		// time+= @CELL_PROGRAM_DELAY
		//		1st cell programming done
		expected_write_duration += CELL_PROGRAM_DELAY;
		//		1st read channel switch start
		// time+= @CHANNEL_SWITCH_DELAY_W
		//		1st read channel switch done
//		expected_write_duration += BLOCK_ERASE_DELAY;
		expected_read_duration += CHANNEL_SWITCH_DELAY_R;
		//		1st register read start
		// time+=@REG_READ_DELAY
		//		1st register read done
		expected_read_duration += REG_READ_DELAY;
		//		1st read programming start
		// time+=@CELL_READ_DELAY
		//		1st read programming done
		expected_read_duration += CELL_READ_DELAY;

		expected_read_duration += REG_READ_DELAY;
		//		1st read programming start
		// time+=@CELL_READ_DELAY
		//		1st read programming done
		expected_read_duration += CELL_READ_DELAY;


		ASSERT_LE(expected_write_duration, SSD_TOTAL_WRITE_DELAY());
		ASSERT_GE(expected_write_duration + bw_delay_threshold(write_ops), SSD_TOTAL_WRITE_DELAY());

		ASSERT_LE(expected_read_duration, SSD_TOTAL_READ_DELAY());
		ASSERT_GE(expected_read_duration + bw_delay_threshold(read_ops), SSD_TOTAL_READ_DELAY());
	}



	// case 11 write on different flash after read
	/**
	 * !
	 * /brief
	 * Write / Read bandwidth
	 * on sequential write erase read
	 */
	TEST_P(SSDIoEmulatorUnitTest, BWCase11) {

		int flash_nb = 0;
		int block_nb = 0;
		int page_nb = 0;

		unsigned long int expected_write_duration, expected_read_duration;
		int write_ops = 1;
		int read_ops = 2;

		SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, IO_PAGE_FIRST);
		SSD_PAGE_READ(flash_nb,block_nb,page_nb,0, READ, IO_PAGE_FIRST);
		SSD_PAGE_WRITE(flash_nb+1,block_nb,page_nb,0, WRITE, IO_PAGE_FIRST);

		//
		// write duration calculation:
		//
		// time=0
		//		1st register write start
		expected_write_duration = 0;
		expected_read_duration = 0;
		// time=@REG_WRITE_DELAY
		//		1st register write done
		expected_write_duration += REG_WRITE_DELAY;
		//		1st cell programming start
		// time+= @CELL_PROGRAM_DELAY
		//		1st cell programming done
		expected_write_duration += CELL_PROGRAM_DELAY;
		//		1st read channel switch start
		// time+= @CHANNEL_SWITCH_DELAY_W
		//		1st read channel switch done
//		expected_write_duration += BLOCK_ERASE_DELAY;
		expected_read_duration += CHANNEL_SWITCH_DELAY_R;
		//		1st register read start
		// time+=@REG_READ_DELAY
		//		1st register read done
		expected_read_duration += REG_READ_DELAY;
		//		1st read programming start
		// time+=@CELL_READ_DELAY
		//		1st read programming done
		expected_read_duration += CELL_READ_DELAY;

		expected_write_duration += REG_WRITE_DELAY;
		//		1st read programming start
		// time+=@CELL_READ_DELAY
		//		1st read programming done
		expected_write_duration += CELL_PROGRAM_DELAY;

		ASSERT_LE(expected_write_duration, SSD_TOTAL_WRITE_DELAY());
		ASSERT_GE(expected_write_duration + bw_delay_threshold(write_ops), SSD_TOTAL_WRITE_DELAY());

		ASSERT_LE(expected_read_duration, SSD_TOTAL_READ_DELAY());
		ASSERT_GE(expected_read_duration + bw_delay_threshold(read_ops), SSD_TOTAL_READ_DELAY());
	}



	// case 12 read on different flash after write
	/**
	 * !
	 * /brief
	 * Write / Read bandwidth
	 * on sequential write erase read
	 */
	TEST_P(SSDIoEmulatorUnitTest, BWCase12) {

		int flash_nb = 0;
		int block_nb = 0;
		int page_nb = 0;

		unsigned long int expected_write_duration, expected_read_duration;
		int write_ops = 2;
		int read_ops = 2;

		SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, IO_PAGE_FIRST);
		SSD_PAGE_READ(flash_nb,block_nb,page_nb,0, READ, IO_PAGE_FIRST);
		SSD_PAGE_WRITE(flash_nb+1,block_nb,page_nb,0, WRITE, IO_PAGE_FIRST);
		SSD_PAGE_READ(flash_nb,block_nb,page_nb,0, READ, IO_PAGE_FIRST);

		//
		// write duration calculation:
		//
		// time=0
		//		1st register write start
		expected_write_duration = 0;
		expected_read_duration = 0;
		// time=@REG_WRITE_DELAY
		//		1st register write done
		expected_write_duration += REG_WRITE_DELAY;
		//		1st cell programming start
		// time+= @CELL_PROGRAM_DELAY
		//		1st cell programming done
		expected_write_duration += CELL_PROGRAM_DELAY;
		//		1st read channel switch start
		// time+= @CHANNEL_SWITCH_DELAY_W
		//		1st read channel switch done
//		expected_write_duration += BLOCK_ERASE_DELAY;
		expected_read_duration += CHANNEL_SWITCH_DELAY_R;
		//		1st register read start
		// time+=@REG_READ_DELAY
		//		1st register read done
		expected_read_duration += REG_READ_DELAY;
		//		1st read programming start
		// time+=@CELL_READ_DELAY
		//		1st read programming done
		expected_read_duration += CELL_READ_DELAY;

		expected_write_duration += REG_WRITE_DELAY;
		//		1st read programming start
		// time+=@CELL_READ_DELAY
		//		1st read programming done
		expected_write_duration += CELL_PROGRAM_DELAY;

		expected_read_duration += REG_READ_DELAY;
		expected_read_duration += CELL_READ_DELAY;

		ASSERT_LE(expected_write_duration, SSD_TOTAL_WRITE_DELAY());
		ASSERT_GE(expected_write_duration + bw_delay_threshold(write_ops), SSD_TOTAL_WRITE_DELAY());

		ASSERT_LE(expected_read_duration, SSD_TOTAL_READ_DELAY());
		ASSERT_GE(expected_read_duration + bw_delay_threshold(read_ops), SSD_TOTAL_READ_DELAY());
	}



	/***
	 * With io overhead
	 */

	/**
	 * Case 13. write after write. diff < reg write
	 */
	TEST_P(SSDIoEmulatorUnitTest, BWCase13) {

		int flash_nb = 0;
		int block_nb = 0;
		int page_nb = 0;

		int usec_diff_min = 20;
		int usec_diff_max = 40;
		unsigned long int expected_duration = 0;
		int write_ops = 2;

		// validate @usec_diff < @REG_WRITE_DELAY
		ASSERT_LT(usec_diff_min, REG_WRITE_DELAY);
		ASSERT_LT(usec_diff_max, REG_WRITE_DELAY);


		int64_t th1_started;
		int64_t th2_started;
		int usec_diff = 0;

		do {

			SSD_IO_INIT();

			int64_t current_timestamp = get_usec();

			// run SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, -1) method
			// at time: @(current_timestamp + 1000)
			pthread_t* th1 = run_ssd_page_write_thread(current_timestamp + 1000
					, &th1_started
					, flash_nb,block_nb,page_nb,0, WRITE, IO_PAGE_FIRST);
			// run SSD_PAGE_WRITE(flash_nb,block_nb,page_nb+1,0, WRITE, -1) method
			// at time: @(current_timestamp + 1000 + usec_diff)
			pthread_t* th2 = run_ssd_page_write_thread(current_timestamp + 1000 + usec_diff_min
					, &th2_started
					, flash_nb,block_nb,page_nb+1,0, WRITE, IO_PAGE_FIRST);

			pthread_join(*th1, NULL);
			pthread_join(*th2, NULL);

			usec_diff = th2_started - th1_started;


			//
			// duration calculation:
			//
			// time=0
			//		1st register write start
			expected_duration = 0;
			// time=@usec_diff
			//		1st page write left @REG_WRITE_DELAY + @CELL_PROGRAM_DELAY - @usec_diff
			//		2nd wait
			expected_duration += REG_WRITE_DELAY + CELL_PROGRAM_DELAY - usec_diff;
			// time=@REG_WRITE_DELAY
			//		1st register write done
			expected_duration += REG_WRITE_DELAY;
			//		2nd register write start
			//		1st cell programming start
			// time=@REG_WRITE_DELAY + @REG_WRITE_DELAY
			//		2nd write register done
	//		expected_duration += REG_WRITE_DELAY;
			//		1st cell programming left @CELL_PROGRAM_DELAY - @REG_WRITE_DELAY
			//		2nd cell programming wait
	//		expected_duration += CELL_PROGRAM_DELAY - REG_WRITE_DELAY;
			// time=@REG_WRITE_DELAY + @CELL_PROGRAM_DELAY
			//		1st cell programming done
			expected_duration += CELL_PROGRAM_DELAY;
			//		2nd cell programming start
			// time=@REG_WRITE_DELAY + @CELL_PROGRAM_DELAY * 2
			//		2nd cell programming done
			expected_duration += REG_WRITE_DELAY + CELL_PROGRAM_DELAY;

			if( usec_diff < REG_WRITE_DELAY ) {
				ASSERT_LE(expected_duration, SSD_TOTAL_WRITE_DELAY());
				ASSERT_GE(expected_duration + bw_delay_threshold(write_ops), SSD_TOTAL_WRITE_DELAY());
				break;
			}

		} while(1);
	}


	/**
	 * Case 14. write after write. diff > reg write && diff < reg_write + cell program
	 */
	TEST_P(SSDIoEmulatorUnitTest, BWCase14) {

		int flash_nb = 0;
		int block_nb = 0;
		int page_nb = 0;

		int usec_diff_min = 200;
		int usec_diff_max = 400;

		unsigned long int expected_duration = 0;
		int write_ops = 2;

		// validate @usec_diff >= @REG_WRITE_DELAY
		ASSERT_GE(usec_diff_min, REG_WRITE_DELAY);
		ASSERT_GE(usec_diff_max, REG_WRITE_DELAY);
		// validate @usec_diff + @REG_WRITE_DELAY  < @CELL_PROGRAM_DELAY
		ASSERT_LT(usec_diff_min + REG_WRITE_DELAY, CELL_PROGRAM_DELAY);
		ASSERT_LT(usec_diff_max + REG_WRITE_DELAY, CELL_PROGRAM_DELAY);


		int64_t th1_started;
		int64_t th2_started;
		int usec_diff = 0;

		do {

			SSD_IO_INIT();

			usleep(100000);

			int64_t current_timestamp = get_usec();
			// run SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, -1) method
			// at time: @(current_timestamp + 1000)
			pthread_t* th1 = run_ssd_page_write_thread(current_timestamp + 100000
					, &th1_started
					, flash_nb,block_nb,page_nb,0, WRITE, IO_PAGE_FIRST);
			// run SSD_PAGE_WRITE(flash_nb,block_nb,page_nb+1,0, WRITE, -1) method
			// at time: @(current_timestamp + 1000 + usec_diff)
			pthread_t* th2 = run_ssd_page_write_thread(current_timestamp + 100000 + usec_diff_min
					, &th2_started
					, flash_nb,block_nb,page_nb+1,0, WRITE, IO_PAGE_FIRST);

			pthread_join(*th1, NULL);
			pthread_join(*th2, NULL);

			usec_diff = th2_started - th1_started;
			printf("usec_diff: %d\n", usec_diff);


			//
			// duration calculation:
			//
			// time=0
			//		1st register write start
			expected_duration = 0;
			// time=@REG_WRITE_DELAY
			//		1st register write done
			expected_duration += REG_WRITE_DELAY;
			//		1st cell programming start
			// time=@usec_diff
			//		2nd register write start
			// time=@usec_diff + @REG_WRITE_DELAY
			//		2nd register write done
	//		expected_duration += REG_WRITE_DELAY;
			//		1st cell program left @CELL_PROGRAM_DELAY - @usec_diff
			//		2n wait
			expected_duration += REG_WRITE_DELAY + CELL_PROGRAM_DELAY - usec_diff;
			// time=@REG_WRITE_DELAY + @CELL_PROGRAM_DELAY
			//		1st cell program done
			expected_duration += CELL_PROGRAM_DELAY;
			//		2nd cell program start
			// time=@REG_WRITE_DELAY + @CELL_PROGRAM_DELAY * 2
			//		2nd cell program done
			expected_duration += REG_WRITE_DELAY + CELL_PROGRAM_DELAY;

			if( usec_diff >= usec_diff_min && usec_diff <= usec_diff_max) {
				ASSERT_LE(expected_duration, SSD_TOTAL_WRITE_DELAY());
				ASSERT_GE(expected_duration + bw_delay_threshold(write_ops), SSD_TOTAL_WRITE_DELAY());
				break;
			}

		} while(1);
	}

	/**
	 * Case 15. read after write. diff < reg write
	 */
	TEST_P(SSDIoEmulatorUnitTest, BWCase15) {

		int flash_nb = 0;
		int block_nb = 0;
		int page_nb = 0;

		int usec_diff_min = 20;
		int usec_diff_max = 70;

		unsigned long int expected_write_duration, expected_read_duration;

		int write_ops = 1;
		int read_ops = 1;

		ASSERT_LT(usec_diff_min, REG_WRITE_DELAY);
		ASSERT_LT(usec_diff_max, REG_WRITE_DELAY);

		int64_t th1_started;
		int64_t th2_started;
		int usec_diff;

		do {

			usleep(100000);

			int64_t current_timestamp = get_usec();

			SSD_IO_INIT();

			// run SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, -1) method
			// at time: @(current_timestamp + THREADS_START_DELAY)
			pthread_t* th1 = run_ssd_page_write_thread(current_timestamp + THREADS_START_DELAY
					, &th1_started
					, flash_nb,block_nb,page_nb,0, WRITE, IO_PAGE_FIRST);
			// run SSD_PAGE_READ(flash_nb,block_nb,page_nb+1,0, READ, -1) method
			// at time: @(current_timestamp + THREADS_START_DELAY + usec_diff)
			pthread_t* th2 = run_ssd_page_read_thread(current_timestamp + THREADS_START_DELAY + usec_diff_min
					, &th2_started
					, flash_nb,block_nb,page_nb,0, READ, IO_PAGE_FIRST);

			pthread_join(*th1, NULL);
			pthread_join(*th2, NULL);

			usec_diff = th2_started - th1_started;

			// validate @usec_diff < @REG_WRITE_DELAY
			//
			// duration calculation:
			//
			// time=0
			expected_write_duration = 0;
			expected_read_duration = 0;
			//		1st register write start
			// time=@usec_diff
			//		1st register write left @CELL_PROGRAM_DELAY + @REG_WRITE_DELAY - @usec_diff
			//		2nd wait
			expected_read_duration += CELL_PROGRAM_DELAY + REG_WRITE_DELAY - usec_diff;
			// time=@REG_WRITE_DELAY
			//		1st register write done
			expected_write_duration += REG_WRITE_DELAY;
			//		channel read switch start
			// time=@REG_WRITE_DELAY + CHANNEL_SWITCH_DELAY_R
			//		channel read switch done
			expected_read_duration += CHANNEL_SWITCH_DELAY_R;
			//		register read start
			//		1st cell programming start
			// time=@REG_WRITE_DELAY + @CHANNEL_SWITCH_DELAY_R + @REG_READ_DELAY
			//		2nd write register done
			expected_read_duration += REG_READ_DELAY;
			//		1st cell programming left @CELL_PROGRAM_DELAY - @CHANNEL_SWITCH_DELAY_R - @REG_WRITE_DELAY
			//		2nd cell programming wait
	//		expected_read_duration += CELL_PROGRAM_DELAY - CHANNEL_SWITCH_DELAY_R - REG_WRITE_DELAY;
			// time=@REG_WRITE_DELAY + @CELL_PROGRAM_DELAY
			//		1st cell programming done
			expected_write_duration += CELL_PROGRAM_DELAY;
			//		2nd cell programming start
			// time=@REG_WRITE_DELAY + @CELL_PROGRAM_DELAY * 2
			//		2nd cell programming done
			expected_read_duration += CELL_READ_DELAY;

			if( usec_diff >= usec_diff_min && usec_diff <= usec_diff_max) {
				ASSERT_LE(expected_write_duration, SSD_TOTAL_WRITE_DELAY());
				ASSERT_GE(expected_write_duration + bw_delay_threshold(write_ops), SSD_TOTAL_WRITE_DELAY());

				ASSERT_LE(expected_read_duration, SSD_TOTAL_READ_DELAY());
				ASSERT_GE(expected_read_duration + bw_delay_threshold(read_ops), SSD_TOTAL_READ_DELAY());
				break;
			}
		} while(1);
	}

	/**
	 * Case 16. read after write. diff > reg write & diff + reg_write < cell program
	 */
	TEST_P(SSDIoEmulatorUnitTest, BWCase16) {

		int flash_nb = 0;
		int block_nb = 0;
		int page_nb = 0;

		int usec_diff_min = 200;
		int usec_diff_max = 400;

		unsigned long int expected_write_duration, expected_read_duration;

		int write_ops = 1;
		int read_ops = 1;

		// validate @usec_diff >= @REG_WRITE_DELAY
		ASSERT_GE(usec_diff_min, REG_WRITE_DELAY);
		ASSERT_GE(usec_diff_max, REG_WRITE_DELAY);
		// validate @usec_diff + @CHANNEL_SWITCH_DELAY_R + @REG_WRITE_DELAY  < @CELL_PROGRAM_DELAY
		ASSERT_LT(usec_diff_min + CHANNEL_SWITCH_DELAY_R + REG_WRITE_DELAY, CELL_PROGRAM_DELAY);
		ASSERT_LT(usec_diff_max + CHANNEL_SWITCH_DELAY_R + REG_WRITE_DELAY, CELL_PROGRAM_DELAY);


		int64_t th1_started;
		int64_t th2_started;
		int usec_diff;

		do {

			SSD_IO_INIT();

			int64_t current_timestamp = get_usec();


			// run SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, -1) method
			// at time: @(current_timestamp + THREADS_START_DELAY)
			pthread_t* th1 = run_ssd_page_write_thread(current_timestamp + THREADS_START_DELAY
					, &th1_started
					, flash_nb,block_nb,page_nb,0, WRITE, IO_PAGE_FIRST);
			// run SSD_PAGE_READ(flash_nb,block_nb,page_nb+1,0, READ, -1) method
			// at time: @(current_timestamp + THREADS_START_DELAY + usec_diff)
			pthread_t* th2 = run_ssd_page_read_thread(current_timestamp + THREADS_START_DELAY + usec_diff_min
					, &th2_started
					, flash_nb,block_nb,page_nb+1,0, READ, IO_PAGE_FIRST);

			pthread_join(*th1, NULL);
			pthread_join(*th2, NULL);

			usec_diff = th2_started - th1_started;

			//
			// duration calculation:
			//
			// time=0
			//		1st register write start
			expected_write_duration = 0;
			expected_read_duration = 0;
			// time=@REG_WRITE_DELAY
			//		1st register write done
			expected_write_duration += REG_WRITE_DELAY;
			//		1st cell programming start
			// time=@usec_diff
			//		channel switch read start
			// time=@usec_diff + @CHANNEL_SWITCH_DELAY_R
			//		channel switch read done
	//		expected_read_duration += CHANNEL_SWITCH_DELAY_R;
			//		register read start
			// time=@usec_diff + @CHANNEL_SWITCH_DELAY_R + @REG_READ_DELAY
			//		register read done
	//		expected_read_duration += REG_READ_DELAY;
			//		cell program left program left (@REG_WRITE_DELAY + @CELL_PROGRAM_DELAY - (@usec_diff + @CHANNEL_SWITCH_DELAY_R + @REG_READ_DELAY))
			//		cell read wait
			expected_read_duration += REG_WRITE_DELAY + CELL_PROGRAM_DELAY - (usec_diff );
			// time=@REG_WRITE_DELAY + @CELL_PROGRAM_DELAY
			//		cell program done
			expected_write_duration += CELL_PROGRAM_DELAY;
			//		cell read start
			// time=@REG_WRITE_DELAY + @CELL_PROGRAM_DELAY + @CELL_READ_DELAY
			expected_read_duration += CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY + CELL_READ_DELAY;

			if( usec_diff >= usec_diff_min && usec_diff <= usec_diff_max) {

				ASSERT_LE(expected_write_duration, SSD_TOTAL_WRITE_DELAY());
				ASSERT_GE(expected_write_duration + bw_delay_threshold(write_ops), SSD_TOTAL_WRITE_DELAY());

				ASSERT_LE(expected_read_duration, SSD_TOTAL_READ_DELAY());
				ASSERT_GE(expected_read_duration + bw_delay_threshold(read_ops), SSD_TOTAL_READ_DELAY());
				break;
			}

		} while(1);
	}



	/**
	 * Case 17. write after read. diff < reg read + switch read
	 */
	TEST_P(SSDIoEmulatorUnitTest, BWCase17) {

		int flash_nb = 0;
		int block_nb = 0;
		int page_nb = 0;

		int usec_diff_min = 30;
		int usec_diff_max = 60;

		unsigned long int expected_write_duration, expected_read_duration;

		int write_ops = 2;
		int read_ops = 1;

		CELL_READ_DELAY = 900;

		// validate @usec_diff < @REG_WRITE_DELAY
		ASSERT_LT(usec_diff_min, CHANNEL_SWITCH_DELAY_R + REG_WRITE_DELAY);
		ASSERT_LT(usec_diff_max, CHANNEL_SWITCH_DELAY_R + REG_WRITE_DELAY);

		do {

			int64_t th1_started;
			int64_t th2_started;

			SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, IO_PAGE_FIRST);

			int64_t current_timestamp = get_usec();

			// run SSD_PAGE_READ(flash_nb,block_nb,page_nb,0, READ, -1) method
			// at time: @(current_timestamp + 1000 + usec_diff)
			pthread_t* th1 = run_ssd_page_read_thread(current_timestamp + THREADS_START_DELAY
					, &th1_started
					, flash_nb,block_nb,page_nb,0, READ, IO_PAGE_FIRST);
			// run SSD_PAGE_WRITE(flash_nb,block_nb+1,page_nb,0, WRITE, -1) method
			// at time: @(current_timestamp + 1000)
			pthread_t* th2 = run_ssd_page_write_thread(current_timestamp + THREADS_START_DELAY  + usec_diff_min
					, &th2_started
					, flash_nb,block_nb+1,page_nb,0, WRITE, IO_PAGE_FIRST);

			pthread_join(*th1, NULL);
			pthread_join(*th2, NULL);

			int usec_diff = th2_started - th1_started;

			//
			// duration calculation:
			//
			// time=0
			//		1st page write done
			expected_write_duration = REG_WRITE_DELAY + CELL_PROGRAM_DELAY;
			expected_read_duration = 0;
			//		channel read switch start
			// time=@CHANNEL_SWITCH_DELAY_R
			//		channel read switch done
			expected_read_duration += CHANNEL_SWITCH_DELAY_R;
			// 		register read start
			// time=@usec_diff
			//		register read left (CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY - @usec_diff)
			//		channel switch write wait
			expected_write_duration += CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY + CELL_READ_DELAY - usec_diff;
			// time=@CHANNEL_SWITCH_DELAY_R + @REG_READ_DELAY
			//		register read done
			expected_read_duration += REG_READ_DELAY;
			//		channel switch write start
			//		cell read start
			// time=@CHANNEL_SWITCH_DELAY_R + @REG_READ_DELAY + CHANNEL_SWITCH_DELAY_W
			//		channel switch write done
			expected_write_duration += CHANNEL_SWITCH_DELAY_W;
			//		register write start
			// time=@CHANNEL_SWITCH_DELAY_R + @REG_READ_DELAY + CHANNEL_SWITCH_DELAY_W + @REG_WRITE_DELAY
			//		register write done
			expected_write_duration += REG_WRITE_DELAY;
			//		cell read left (CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY + CELL_READ_DELAY) - (CHANNEL_SWITCH_DELAY_R + @REG_READ_DELAY + CHANNEL_SWITCH_DELAY_W + @REG_WRITE_DELAY)
			//		cell program wait
	//		expected_write_duration += (CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY + CELL_READ_DELAY) - (CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY + CHANNEL_SWITCH_DELAY_W + REG_WRITE_DELAY);
			// time=@CHANNEL_SWITCH_DELAY_R + @REG_READ_DELAY + CELL_READ_DELAY
			//		cell read done
			expected_read_duration += CELL_READ_DELAY;
			//		cell program start
			// time=@CHANNEL_SWITCH_DELAY_R + @REG_READ_DELAY + CELL_READ_DELAY + CELL_PROGRAM_DELAY
			//		cell program done
			expected_write_duration += CELL_PROGRAM_DELAY;


			if( usec_diff >= usec_diff_min && usec_diff <= usec_diff_max ) {

				ASSERT_LE(expected_write_duration, SSD_TOTAL_WRITE_DELAY());
				ASSERT_GE(expected_write_duration + bw_delay_threshold(write_ops), SSD_TOTAL_WRITE_DELAY());

				ASSERT_LE(expected_read_duration, SSD_TOTAL_READ_DELAY());
				ASSERT_GE(expected_read_duration + bw_delay_threshold(read_ops), SSD_TOTAL_READ_DELAY());
				break;
			}

		} while(1);



	}


	/**
	 * Case 18. write after read. diff > reg read + switch read && diff < reg read + + switch read + cell read
	 */
	TEST_P(SSDIoEmulatorUnitTest, BWCase18) {

		int flash_nb = 0;
		int block_nb = 0;
		int page_nb = 0;

		int usec_diff_min = 100;
		int usec_diff_max = 150;

		unsigned long int expected_write_duration, expected_read_duration;

		int write_ops = 1;
		int read_ops = 1;

		CELL_READ_DELAY = 200;
		// validate @usec_diff >= @REG_WRITE_DELAY
		ASSERT_GE(usec_diff_min, CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY);
		ASSERT_GE(usec_diff_max, CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY);

		// validate @usec_diff + @CHANNEL_SWITCH_DELAY_R + @REG_WRITE_DELAY  < @CELL_PROGRAM_DELAY
		ASSERT_LT(usec_diff_min + CHANNEL_SWITCH_DELAY_W + REG_WRITE_DELAY, CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY + CELL_READ_DELAY);
		ASSERT_LT(usec_diff_max + CHANNEL_SWITCH_DELAY_W + REG_WRITE_DELAY, CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY + CELL_READ_DELAY);

		do {

			SSD_IO_INIT();

			SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, IO_PAGE_FIRST);

			int64_t current_timestamp = get_usec();

			int64_t th1_started;
			int64_t th2_started;

			// run SSD_PAGE_READ(flash_nb,block_nb,page_nb+1,0, READ, -1) method
			// at time: @(current_timestamp + 1000 + usec_diff)
			pthread_t* th2 = run_ssd_page_read_thread(current_timestamp + 100000
					, &th1_started
					, flash_nb,block_nb,page_nb+1,0, READ, IO_PAGE_FIRST);
			// run SSD_PAGE_WRITE(flash_nb,block_nb+1,page_nb,0, WRITE, -1) method
			// at time: @(current_timestamp + 1000)
			pthread_t* th1 = run_ssd_page_write_thread(current_timestamp + 100000  + usec_diff_min
					, &th2_started
					, flash_nb,block_nb+1,page_nb,0, WRITE, IO_PAGE_FIRST);

			pthread_join(*th1, NULL);
			pthread_join(*th2, NULL);

			int usec_diff = th2_started - th1_started;

			if( usec_diff >= usec_diff_min && usec_diff <= usec_diff_max ) {

				//
				// duration calculation:
				//
				// time=0
				//		1st page write done
				expected_write_duration = REG_WRITE_DELAY + CELL_PROGRAM_DELAY;
				expected_read_duration = 0;
				//		channel read switch start
				// time=@CHANNEL_SWITCH_DELAY_R
				//		channel read switch done
				expected_read_duration += CHANNEL_SWITCH_DELAY_R;
				// 		register read start
				// time=@CHANNEL_SWITCH_DELAY_R + @REG_READ_DELAY
				//		register read done
				expected_read_duration += REG_READ_DELAY;
				//		cell read start
				// time=@usec_diff
				//		page read left CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY + CELL_READ_DELAY - usec_diff
				//		write page wait
				expected_write_duration += CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY + CELL_READ_DELAY - usec_diff;
				//		channel
				// time=@usec_diff + @CHANNEL_SWITCH_DELAY_W
				//		channel switch write done
				expected_write_duration += CHANNEL_SWITCH_DELAY_W;
				//		2nd register write start
				// time=@usec_diff + @CHANNEL_SWITCH_DELAY_W + @REG_WRITE_DELAY
				//		2nd register write done
				expected_write_duration += REG_WRITE_DELAY;
				//		cell read left (@CHANNEL_SWITCH_DELAY_R + @REG_READ_DELAY + @CELL_READ_DELAY - (@usec_diff + @CHANNEL_SWITCH_DELAY_W + @REG_WRITE_DELAY))
				//		2nd write wait
		//		expected_write_duration += (CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY + CELL_READ_DELAY - (usec_diff + CHANNEL_SWITCH_DELAY_W + REG_WRITE_DELAY));
				// time=@REG_READ_DELAY + @CELL_READ_DELAY
				//		cell read done
				expected_read_duration += CELL_READ_DELAY;
				// 		2nd cell program start
				// time = @REG_READ_DELAY + @CELL_READ_DELAY + @CELL_PROGRAM_DELAY;
				expected_write_duration += CELL_PROGRAM_DELAY;


				ASSERT_LE(expected_write_duration, SSD_TOTAL_WRITE_DELAY());
				ASSERT_GE(expected_write_duration + bw_delay_threshold(write_ops), SSD_TOTAL_WRITE_DELAY());

				ASSERT_LE(expected_read_duration, SSD_TOTAL_READ_DELAY());
				ASSERT_GE(expected_read_duration + bw_delay_threshold(read_ops), SSD_TOTAL_READ_DELAY());

				break;
			}

		} while(1);
	}


	/**
	 * Case 19. read after read. diff < reg read + switch r
	 */
	TEST_P(SSDIoEmulatorUnitTest, BWCase19) {

		int flash_nb = 0;
		int block_nb = 0;
		int page_nb = 0;

		int usec_diff_min = 100;
		int usec_diff_max = 150;

		unsigned long int expected_write_duration = 0;
		unsigned long int expected_read_duration = 0;
		int write_ops = 1;
		int read_ops = 2;

		CELL_READ_DELAY = 200;
		// validate @usec_diff >= @REG_WRITE_DELAY
		ASSERT_GT(usec_diff_min, CHANNEL_SWITCH_DELAY_R);
		ASSERT_GT(usec_diff_min, CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY);
		ASSERT_LT(usec_diff_min + REG_READ_DELAY, CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY + CELL_READ_DELAY);

		ASSERT_GT(usec_diff_max, CHANNEL_SWITCH_DELAY_R);
		ASSERT_GT(usec_diff_max, CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY);
		ASSERT_LT(usec_diff_max + REG_READ_DELAY, CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY + CELL_READ_DELAY);


		do {

			SSD_IO_INIT();

			SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, IO_PAGE_FIRST);

			int64_t current_timestamp = get_usec();

			int64_t th1_started;
			int64_t th2_started;

			// run SSD_PAGE_READ(flash_nb,block_nb,page_nb,0, READ, -1) method
			// at time: @(current_timestamp + 1000 + usec_diff)
			pthread_t* th1 = run_ssd_page_read_thread(current_timestamp + 100000
					, &th1_started
					, flash_nb,block_nb,page_nb,0, READ, IO_PAGE_FIRST);
			// run SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, -1) method
			// at time: @(current_timestamp + 1000)
			pthread_t* th2 = run_ssd_page_read_thread(current_timestamp + 100000  + usec_diff_min
					, &th2_started
					, flash_nb,block_nb,page_nb,0, READ, IO_PAGE_FIRST);

			pthread_join(*th1, NULL);
			pthread_join(*th2, NULL);

			int usec_diff = th2_started - th1_started;

			if( usec_diff >= usec_diff_min && usec_diff <= usec_diff_max ) {

				//
				// duration calculation:
				//
				// time=0
				expected_write_duration = REG_WRITE_DELAY + CELL_PROGRAM_DELAY;
				expected_read_duration = 0;
				//		channel read switch start
				// time=@CHANNEL_SWITCH_DELAY_R
				//		channel read switch done
				expected_read_duration += CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY + CELL_READ_DELAY;
				expected_read_duration += REG_READ_DELAY + CELL_READ_DELAY;
				expected_read_duration += CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY + CELL_READ_DELAY - usec_diff;
				// 		1st register read start
				// time=@CHANNEL_SWITCH_DELAY_R + @REG_READ_DELAY
				//		1st page register done
		//		expected_read_duration += REG_READ_DELAY;
				//		1st cell read start
				// time=@usec_diff
				//		2nd page register start
				// time=@usec_diff + REG_READ_DELAY
				//		2nd page register done
		//		expected_read_duration += REG_READ_DELAY;
				//		1st cell read left CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY + CELL_READ_DELAY - (usec_diff + REG_READ_DELAY)
				//		2nd cell read wait
		//		expected_read_duration += CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY + CELL_READ_DELAY - (usec_diff + REG_READ_DELAY);
				// time=CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY + CELL_READ_DELAY
				//		1st cell read done
		//		expected_read_duration += CELL_READ_DELAY;
				//		2nd cell read start
				// time=CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY + CELL_READ_DELAY + CELL_READ_DELAY
				//		2nd cell read done
		//		expected_read_duration += CELL_READ_DELAY;


				ASSERT_LE(expected_write_duration, SSD_TOTAL_WRITE_DELAY());
				ASSERT_GE(expected_write_duration + bw_delay_threshold(write_ops), SSD_TOTAL_WRITE_DELAY());

				ASSERT_LE(expected_read_duration, SSD_TOTAL_READ_DELAY());
				ASSERT_GE(expected_read_duration + bw_delay_threshold(read_ops), SSD_TOTAL_READ_DELAY());

				break;
			}

		} while(1);

	}

	/**
	 * Case 20. read after read. diff > reg read + switch read & diff < reg read + switch read + cell read
	 */
	TEST_P(SSDIoEmulatorUnitTest, BWCase20) {

		int flash_nb = 0;
		int block_nb = 0;
		int page_nb = 0;

		int usec_diff_min = 100;
		int usec_diff_max = 200;
		unsigned long int expected_read_duration = 0;
		int write_ops = 1;
		int read_ops = 2;

		CELL_READ_DELAY = 200;
		// validate @usec_diff >= @REG_WRITE_DELAY
		ASSERT_GT(usec_diff_min, CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY);
		ASSERT_LT(usec_diff_min + REG_READ_DELAY, CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY + CELL_READ_DELAY);

		ASSERT_GT(usec_diff_max, CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY);
		ASSERT_LT(usec_diff_max + REG_READ_DELAY, CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY + CELL_READ_DELAY);

		do {

			int64_t current_timestamp = get_usec();

			SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, IO_PAGE_FIRST);

			int64_t th1_started;
			int64_t th2_started;

			// run SSD_PAGE_READ(flash_nb,block_nb,page_nb,0, READ, -1) method
			// at time: @(current_timestamp + 1000 + usec_diff)
			pthread_t* th1 = run_ssd_page_read_thread(current_timestamp + 100000
					, &th1_started
					, flash_nb,block_nb,page_nb,0, READ, IO_PAGE_FIRST);
			// run SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, -1) method
			// at time: @(current_timestamp + 1000)
			pthread_t* th2 = run_ssd_page_read_thread(current_timestamp + 100000  + usec_diff_min
					, &th2_started
					, flash_nb,block_nb,page_nb,0, READ, IO_PAGE_FIRST);

			pthread_join(*th1, NULL);
			pthread_join(*th2, NULL);

			int usec_diff = th2_started - th1_started;

			//
			// duration calculation:
			//
			// time=0
			expected_read_duration += CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY + CELL_READ_DELAY;
			expected_read_duration += REG_READ_DELAY + CELL_READ_DELAY;
			expected_read_duration += CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY + CELL_READ_DELAY - usec_diff;

			if( usec_diff >= usec_diff_min && usec_diff <= usec_diff_max ) {

				ASSERT_LE(expected_read_duration, SSD_TOTAL_READ_DELAY());
				ASSERT_GE(expected_read_duration + bw_delay_threshold(read_ops), SSD_TOTAL_READ_DELAY());
				break;
			}

		} while(1);
	}

	/**
	 * Case 21. write after erase. diff + reg write < block erase
	 */
	TEST_P(SSDIoEmulatorUnitTest, BWCase21) {

		int flash_nb = 0;
		int block_nb = 0;
		int page_nb = 0;

		int usec_diff_min = 100;
		int usec_diff_max = 200;

		unsigned long int expected_write_duration = 0;
		int write_ops = 3;
		int read_ops = 0;

		BLOCK_ERASE_DELAY = 2000;

		ASSERT_LT(usec_diff_min + REG_WRITE_DELAY, BLOCK_ERASE_DELAY);
		ASSERT_LT(usec_diff_max + REG_WRITE_DELAY, BLOCK_ERASE_DELAY);

		do {

			SSD_IO_INIT();

			SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, IO_PAGE_FIRST);

			int64_t th1_started;
			int64_t th2_started;

			int64_t current_timestamp = get_usec();

			// run SSD_BLOCK_ERASE(flash_nb,block_nb) method
			// at time: @(current_timestamp + 1000 + usec_diff)
			pthread_t* th1 = run_ssd_block_erase_thread(current_timestamp + 100000
					, &th1_started
					, flash_nb,block_nb);
			// run SSD_PAGE_WRITE(0,0,0,0, WRITE, -1) method
			// at time: @(current_timestamp + 1000)
			pthread_t* th2 = run_ssd_page_write_thread(current_timestamp + 100000  + usec_diff_min
					, &th2_started
					, flash_nb,block_nb,page_nb,0, WRITE, IO_PAGE_FIRST);

			pthread_join(*th1, NULL);
			pthread_join(*th2, NULL);

			int usec_diff = th2_started - th1_started;


			//
			// duration calculation:
			//
			// time=0
			expected_write_duration = REG_WRITE_DELAY + CELL_PROGRAM_DELAY;
			//		erase start
			// time = usec_diff
			//		reg write start
			// time = usec_diff + REG_WRITE_DELAY
			//		reg write done
			expected_write_duration += REG_WRITE_DELAY;
			//		cell prog wait, erase left (BLOCK_ERASE_DELAY - REG_WRITE_DELAY - usec_diff)
			expected_write_duration += BLOCK_ERASE_DELAY - REG_WRITE_DELAY - usec_diff;
			// time = BLOCK_ERASE_DELAY
			//		erase done
			expected_write_duration += BLOCK_ERASE_DELAY;
			//		cell prog start
			// time = BLOCK_ERASE_DELAY + CELL_PROGRAM_DELAY
			//		cell prog done
			expected_write_duration += CELL_PROGRAM_DELAY;


			expected_write_duration = REG_WRITE_DELAY + CELL_PROGRAM_DELAY;
			expected_write_duration += BLOCK_ERASE_DELAY;
			expected_write_duration += BLOCK_ERASE_DELAY - usec_diff - REG_WRITE_DELAY;
			expected_write_duration += REG_WRITE_DELAY + CELL_PROGRAM_DELAY;

//			expected_write_duration += BLOCK_ERASE_DELAY;
//			expected_write_duration += REG_WRITE_DELAY + CELL_PROGRAM_DELAY;
//			expected_write_duration += BLOCK_ERASE_DELAY - (usec_diff + REG_WRITE_DELAY);
			printf("expected_write_duration: %d\n", expected_write_duration);
	//		//		block erase start
	//		//
	//		// time=usec_diff
	//		//		register write start
	//		// time=usec_diff + REG_WRITE_DELAY
	//		//		register write done
	//		expected_write_duration += REG_WRITE_DELAY;
	//		//		block erase left BLOCK_ERASE_DELAY - (usec_diff + REG_WRITE_DELAY)
	//		//		cell program wait
	//		expected_write_duration += BLOCK_ERASE_DELAY - (usec_diff + REG_WRITE_DELAY);
	//		// time=BLOCK_ERASE_DELAY
	//		//		block erase done
	//		expected_write_duration += BLOCK_ERASE_DELAY;
	//		//		cell program start
	//		// time=BLOCK_ERASE_DELAY + CELL_PROGRAM_DELAY
	//		expected_write_duration += CELL_PROGRAM_DELAY;

			if( usec_diff >= usec_diff_min && usec_diff <= usec_diff_max ) {
				ASSERT_LE(expected_write_duration, SSD_TOTAL_WRITE_DELAY());
				ASSERT_GE(expected_write_duration + bw_delay_threshold(write_ops), SSD_TOTAL_WRITE_DELAY());
				break;
			}

		} while(1);

	}

	/**
	 * Case 22. write after erase. diff + REG_WRITE_DELAY < block erase
	 */
	TEST_P(SSDIoEmulatorUnitTest, BWCase22) {

		int flash_nb = 0;
		int block_nb = 0;
		int page_nb = 0;

		int usec_diff_min = 100;
		int usec_diff_max = 150;

		unsigned long int expected_write_duration = 0;
		unsigned long int expected_read_duration = 0;
		int write_ops = 3;
		int read_ops = 1;

		BLOCK_ERASE_DELAY = 2000;

		ASSERT_LT(usec_diff_min + REG_WRITE_DELAY, BLOCK_ERASE_DELAY);
		ASSERT_LT(usec_diff_max + REG_WRITE_DELAY, BLOCK_ERASE_DELAY);

		do {

			SSD_IO_INIT();

			int64_t current_timestamp = get_usec();

			SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, IO_PAGE_FIRST);
			SSD_PAGE_READ(flash_nb,block_nb,page_nb,0, READ, IO_PAGE_FIRST);

			int64_t th1_started;
			int64_t th2_started;

			// run SSD_BLOCK_ERASE(flash_nb,block_nb) method
			// at time: @(current_timestamp + 1000 + usec_diff)
			pthread_t* th1 = run_ssd_block_erase_thread(current_timestamp + 100000
					, &th1_started
					, flash_nb,block_nb);
			// run SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, -1) method
			// at time: @(current_timestamp + 1000)
			pthread_t* th2 = run_ssd_page_write_thread(current_timestamp + 100000  + usec_diff_min
					, &th2_started
					, flash_nb,block_nb,page_nb,0, WRITE, IO_PAGE_FIRST);

			pthread_join(*th1, NULL);
			pthread_join(*th2, NULL);

			int usec_diff = th2_started - th1_started;


			//
			// duration calculation:
			//
			// time=0
			expected_write_duration = REG_WRITE_DELAY + CELL_PROGRAM_DELAY;
			//		block erase start
			//
			// time=usec_diff
			//		channel switch write start
			// time=usec_diff + CHANNEL_SWITCH_DELAY_R
			//		channel switch write done
			//		register write start
			// time=usec_diff + CHANNEL_SWITCH_DELAY_R + REG_WRITE_DELAY
			//		register write done
			expected_write_duration += REG_WRITE_DELAY;
			//		block erase left BLOCK_ERASE_DELAY - (usec_diff + REG_WRITE_DELAY)
			//		cell program wait
			expected_write_duration += BLOCK_ERASE_DELAY - (usec_diff + REG_WRITE_DELAY);
			// time=BLOCK_ERASE_DELAY
			//		block erase done
			expected_write_duration += BLOCK_ERASE_DELAY;
			//		cell program start
			// time=BLOCK_ERASE_DELAY + CELL_PROGRAM_DELAY
			expected_write_duration += CELL_PROGRAM_DELAY;


			if( usec_diff >= usec_diff_min && usec_diff <= usec_diff_max ) {
				ASSERT_LE(expected_write_duration, SSD_TOTAL_WRITE_DELAY());
				ASSERT_GE(expected_write_duration + bw_delay_threshold(write_ops), SSD_TOTAL_WRITE_DELAY());
				break;
			}

		} while(1);
	}


	/**
	 * Case 23. erase after write. diff < reg write
	 *
	 * *** On this case erase will execute before write
	 */
	TEST_P(SSDIoEmulatorUnitTest, BWCase23) {

		int flash_nb = 0;
		int block_nb = 0;
		int page_nb = 0;

		int usec_diff_min = 20;
		int usec_diff_max = 40;

		unsigned long int expected_write_duration = 0;
		unsigned long int expected_read_duration = 0;
		int write_ops = 2;
		int read_ops = 0;

		BLOCK_ERASE_DELAY = 2000;

		ASSERT_LT(usec_diff_min, REG_WRITE_DELAY);
		ASSERT_LT(usec_diff_max, REG_WRITE_DELAY);


		do {

			SSD_IO_INIT();

			int64_t current_timestamp = get_usec();

			int64_t th1_started;
			int64_t th2_started;

			// run SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, -1) method
			// at time: @(current_timestamp + 1000)
			pthread_t* th1 = run_ssd_page_write_thread(current_timestamp + 100000
					, &th1_started
					, flash_nb,block_nb,page_nb,0, WRITE, IO_PAGE_FIRST);
			// run SSD_BLOCK_ERASE(flash_nb,block_nb) method
			// at time: @(current_timestamp + 1000 + usec_diff)
			pthread_t* th2 = run_ssd_block_erase_thread(current_timestamp + 100000  + usec_diff_min
					, &th2_started
					, flash_nb,block_nb);

			pthread_join(*th1, NULL);
			pthread_join(*th2, NULL);

			int usec_diff = th2_started - th1_started;

			//
			// duration calculation:
			//
			// time=0
			expected_write_duration = 0;
			//		reg write start
			// time=usec_diff
			//		page write left (REG_WRITE_DELAY + CELL_PROGRAM_DELAY - usec_diff)
			//		block erase wait
			expected_write_duration += REG_WRITE_DELAY + CELL_PROGRAM_DELAY - usec_diff;
			// time=REG_WRITE_DELAY
			//		reg write done
			expected_write_duration += REG_WRITE_DELAY;
			//		cell program start
			// time=REG_WRITE_DELAY + CELL_PROGRAM_DELAY
			//		cell program done
			expected_write_duration += CELL_PROGRAM_DELAY;
			//		block erase start
			// time=REG_WRITE_DELAY + CELL_PROGRAM_DELAY + BLOCK_ERASE_DELAY
			//		block erase done
			expected_write_duration += BLOCK_ERASE_DELAY;

			if( usec_diff >= usec_diff_min && usec_diff <= usec_diff_max ) {

				ASSERT_LE(expected_write_duration, SSD_TOTAL_WRITE_DELAY());
				ASSERT_GE(expected_write_duration + bw_delay_threshold(write_ops), SSD_TOTAL_WRITE_DELAY());
				break;
			}

		} while(1);
	}


	/**
	 * Case 24. erase after write. diff > reg write & diff < reg write + cell program
	 */
	TEST_P(SSDIoEmulatorUnitTest, BWCase24) {

		int flash_nb = 0;
		int block_nb = 0;
		int page_nb = 0;

		int usec_diff_min = 200;
		int usec_diff_max = 400;

		unsigned long int expected_write_duration = 0;
		unsigned long int expected_read_duration = 0;
		int write_ops = 2;
		int read_ops = 0;

		BLOCK_ERASE_DELAY = 2000;

		ASSERT_GT(usec_diff_min, REG_WRITE_DELAY);
		ASSERT_LT(usec_diff_min, REG_WRITE_DELAY + BLOCK_ERASE_DELAY);

		ASSERT_GT(usec_diff_max, REG_WRITE_DELAY);
		ASSERT_LT(usec_diff_max, REG_WRITE_DELAY + BLOCK_ERASE_DELAY);

		do {

			SSD_IO_INIT();

			int64_t current_timestamp = get_usec();


			int64_t th1_started;
			int64_t th2_started;

			// run SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, -1) method
			// at time: @(current_timestamp + 1000)
			pthread_t* th1 = run_ssd_page_write_thread(current_timestamp + 100000
					, &th1_started
					, flash_nb,block_nb,page_nb,0, WRITE, IO_PAGE_FIRST);
			// run SSD_BLOCK_ERASE(flash_nb,block_nb) method
			// at time: @(current_timestamp + 1000 + usec_diff)
			pthread_t* th2 = run_ssd_block_erase_thread(current_timestamp + 100000 + usec_diff_min
					, &th2_started
					, flash_nb,block_nb);

			pthread_join(*th1, NULL);
			pthread_join(*th2, NULL);

			int usec_diff = th2_started - th1_started;


			//
			// duration calculation:
			//
			// time=0
			expected_write_duration = 0;
			//		reg write start
			// time=REG_WRITE_DELAY
			//		reg write done
			expected_write_duration += REG_WRITE_DELAY;
			//		cell program start
			// time=usec_diff
			//		cell program left REG_WRITE_DELAY + CELL_PROGRAM_DELAY - usec_diff
			//		block erase wait
			expected_write_duration += REG_WRITE_DELAY + CELL_PROGRAM_DELAY - usec_diff;
			// time=REG_WRITE_DELAY + CELL_PROGRAM_DELAY
			//		cell program done
			expected_write_duration += CELL_PROGRAM_DELAY;
			//		block erase start
			// time=REG_WRITE_DELAY + CELL_PROGRAM_DELAY + BLOCK_ERASE_DELAY
			//		block erase done
			expected_write_duration += BLOCK_ERASE_DELAY;
			if( usec_diff >= usec_diff_min && usec_diff <= usec_diff_max ) {

				ASSERT_LE(expected_write_duration, SSD_TOTAL_WRITE_DELAY());
				ASSERT_GE(expected_write_duration + bw_delay_threshold(write_ops), SSD_TOTAL_WRITE_DELAY());
				break;
			}

		} while(1);


	}

	/**
	 * Case 25. erase after read. diff < switch read + reg read
	 */
	TEST_P(SSDIoEmulatorUnitTest, BWCase25) {

		int flash_nb = 0;
		int block_nb = 0;
		int page_nb = 0;

		int usec_diff_min = 20;
		int usec_diff_max = 40;

		unsigned long int expected_write_duration = 0;
		unsigned long int expected_read_duration = 0;
		int write_ops = 2;
		int read_ops = 0;

		BLOCK_ERASE_DELAY = 2000;

		ASSERT_LT(usec_diff_min, REG_WRITE_DELAY);
		ASSERT_LT(usec_diff_max, REG_WRITE_DELAY);


		do {

			SSD_IO_INIT();


			int64_t th1_started;
			int64_t th2_started;

			SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, IO_PAGE_FIRST);

			int64_t current_timestamp = get_usec();

			// run SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, -1) method
			// at time: @(current_timestamp + 1000)
			pthread_t* th1 = run_ssd_page_read_thread(current_timestamp + 100000
					, &th1_started
					, flash_nb,block_nb,page_nb,0, WRITE, IO_PAGE_FIRST);
			// run SSD_BLOCK_ERASE(flash_nb,block_nb) method
			// at time: @(current_timestamp + 1000 + usec_diff)
			pthread_t* th2 = run_ssd_block_erase_thread(current_timestamp + 100000  + usec_diff_min
					, &th2_started
					, flash_nb,block_nb);

			pthread_join(*th1, NULL);
			pthread_join(*th2, NULL);

			int usec_diff = th2_started - th1_started;

			//
			// duration calculation:
			//
			// time=0
			expected_write_duration = REG_WRITE_DELAY + CELL_PROGRAM_DELAY;
			expected_read_duration = 0;
			//		reg write start
			// time=usec_diff
			//		page write left (REG_WRITE_DELAY + CELL_PROGRAM_DELAY - usec_diff)
			//		block erase wait
			expected_write_duration += CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY + CELL_READ_DELAY - usec_diff;
			// time=REG_WRITE_DELAY
			//		reg write done
			expected_read_duration += CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY + CELL_READ_DELAY;
			//		cell program start
			// time=REG_WRITE_DELAY + CELL_PROGRAM_DELAY
			//		cell program done
//			expected_write_duration += CELL_PROGRAM_DELAY;
			//		block erase start
			// time=REG_WRITE_DELAY + CELL_PROGRAM_DELAY + BLOCK_ERASE_DELAY
			//		block erase done
			expected_write_duration += BLOCK_ERASE_DELAY;

			if( usec_diff >= usec_diff_min && usec_diff <= usec_diff_max ) {

				ASSERT_LE(expected_write_duration, SSD_TOTAL_WRITE_DELAY());
				ASSERT_GE(expected_write_duration + bw_delay_threshold(write_ops), SSD_TOTAL_WRITE_DELAY());
				break;
			}

		} while(1);

	}


	/**
	 * Case 26. erase after read. diff > switch read + reg read & diff < switch read + reg read + cell read
	 */
	TEST_P(SSDIoEmulatorUnitTest, BWCase26) {

		int flash_nb = 0;
		int block_nb = 0;
		int page_nb = 0;

		int usec_diff_min = 100;
		int usec_diff_max = 140;

		unsigned long int expected_write_duration = 0;
		unsigned long int expected_read_duration = 0;
		int write_ops = 2;
		int read_ops = 1;

		BLOCK_ERASE_DELAY = 2000;

		ASSERT_GT(usec_diff_min, CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY);
		ASSERT_LT(usec_diff_min, CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY + CELL_READ_DELAY);

		ASSERT_GT(usec_diff_max, CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY);
		ASSERT_LT(usec_diff_max, CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY + CELL_READ_DELAY);

		do {

			SSD_IO_INIT();

			int64_t current_timestamp = get_usec();

			SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, IO_PAGE_FIRST);

			int64_t th1_started;
			int64_t th2_started;
			// run SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, -1) method
			// at time: @(current_timestamp + 1000)
			pthread_t* th1 = run_ssd_page_read_thread(current_timestamp + 100000
					, &th1_started
					, flash_nb,block_nb,page_nb,0, READ, IO_PAGE_FIRST);
			// run SSD_BLOCK_ERASE(flash_nb,block_nb) method
			// at time: @(current_timestamp + 1000 + usec_diff)
			pthread_t* th2 = run_ssd_block_erase_thread(current_timestamp + 100000 + usec_diff_min
					, &th2_started
					, flash_nb,block_nb);

			pthread_join(*th1, NULL);
			pthread_join(*th2, NULL);

			int usec_diff = th2_started - th1_started;

			//
			// duration calculation:
			//
			// time=0
			expected_write_duration = REG_WRITE_DELAY + CELL_PROGRAM_DELAY;
			expected_read_duration = 0;
			//		switch read + reg read start
			// time=CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY
			//		reg write done
			expected_read_duration += CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY;
			//		cell read start
			// time=usec_diff
			//		cell read left CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY + CELL_READ_DELAY - usec_diff
			//		block erase wait
			expected_write_duration += CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY + CELL_READ_DELAY - usec_diff;
			// time=CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY + CELL_READ_DELAY
			//		cell read done
			expected_read_duration += CELL_READ_DELAY;
			//		block erase start
			// time=CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY + CELL_READ_DELAY + BLOCK_ERASE_DELAY
			//		block erase done
			expected_write_duration += BLOCK_ERASE_DELAY;


			if( usec_diff >= usec_diff_min && usec_diff <= usec_diff_max ) {

				ASSERT_LE(expected_write_duration, SSD_TOTAL_WRITE_DELAY());
				ASSERT_GE(expected_write_duration + bw_delay_threshold(write_ops), SSD_TOTAL_WRITE_DELAY());

				ASSERT_LE(expected_read_duration, SSD_TOTAL_READ_DELAY());
				ASSERT_GE(expected_read_duration + bw_delay_threshold(read_ops), SSD_TOTAL_READ_DELAY());
				break;
			}

		} while(1);
	}


	/**
	 * Case 27. erase after read. diff > switch read + reg read & diff < switch read + reg read + cell read
	 */
	TEST_P(SSDIoEmulatorUnitTest, BWCase27) {

		int flash_nb = 0;
		int block_nb = 0;
		int page_nb = 0;

		int usec_diff_min = 100;
		int usec_diff_max = 140;

		unsigned long int expected_write_duration = 0;
		unsigned long int expected_read_duration = 0;
		int write_ops = 3;
		int read_ops = 0;

		BLOCK_ERASE_DELAY = 2000;

		ASSERT_GT(usec_diff_min, CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY);
		ASSERT_LT(usec_diff_min, CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY + CELL_READ_DELAY);

		ASSERT_GT(usec_diff_max, CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY);
		ASSERT_LT(usec_diff_max, CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY + CELL_READ_DELAY);

		do {

			SSD_IO_INIT();

			int64_t current_timestamp = get_usec();

//			SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, IO_PAGE_FIRST);

			int64_t th1_started;
			int64_t th2_started;
			int64_t th3_started;

			// run SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, -1) method
			// at time: @(current_timestamp + 1000)
			pthread_t* th1 = run_ssd_page_write_thread(current_timestamp + 100000
					, &th1_started
					, flash_nb,block_nb,page_nb,0, WRITE, IO_PAGE_FIRST);
			// run SSD_BLOCK_ERASE(flash_nb,block_nb) method
			// at time: @(current_timestamp + 1000 + usec_diff)
			pthread_t* th2 = run_ssd_page_write_thread(current_timestamp + 100000 + usec_diff_min
					, &th2_started
					, flash_nb,block_nb,page_nb+1,0, WRITE, IO_PAGE_FIRST);
//			// run SSD_BLOCK_ERASE(flash_nb,block_nb) method
//			// at time: @(current_timestamp + 1000 + usec_diff)
			pthread_t* th3 = run_ssd_page_write_thread(current_timestamp + 100000 + usec_diff_min * 2
					, &th3_started
					, flash_nb,block_nb,page_nb+2,0, WRITE, IO_PAGE_FIRST);

			pthread_join(*th1, NULL);
			pthread_join(*th2, NULL);
			pthread_join(*th3, NULL);

			int usec_diff_1 = th2_started - th1_started;
			int usec_diff_2 = th3_started - th2_started;

//			printf("usec_diff_1: %d\n", usec_diff_1);
//			printf("usec_diff_2: %d\n", usec_diff_2);
			//
			// duration calculation:
			//
			// time=0
			// 		w1 start
			// time = usec_diff_1
			//		w2 wait, w1 left REG_WRITE_DELAY + CELL_PROGRAM_DELAY - usec_diff_1
			expected_write_duration += REG_WRITE_DELAY + CELL_PROGRAM_DELAY - usec_diff_1;
			// time = usec_diff_1 + usec_diff_2
			//		w3 wait, w1 left REG_WRITE_DELAY + CELL_PROGRAM_DELAY - usec_diff_1 - usec_diff_2
			expected_write_duration += REG_WRITE_DELAY + CELL_PROGRAM_DELAY - usec_diff_1 - usec_diff_2;
			// time = REG_WRITE_DELAY + CELL_PROGRAM_DELAY
			//		w1 done
			expected_write_duration += REG_WRITE_DELAY + CELL_PROGRAM_DELAY;
			//		w2 start
			//		w3 wait, w2 left REG_WRITE_DELAY + CELL_PROGRAM_DELAY
			expected_write_duration += REG_WRITE_DELAY + CELL_PROGRAM_DELAY;
			// time = (REG_WRITE_DELAY + CELL_PROGRAM_DELAY) * 2
			//		w2 done
			expected_write_duration += REG_WRITE_DELAY + CELL_PROGRAM_DELAY;
			//		w3 start
			// time = (REG_WRITE_DELAY + CELL_PROGRAM_DELAY) * 3
			//		w3 done
			expected_write_duration += REG_WRITE_DELAY + CELL_PROGRAM_DELAY;


//			expected_write_duration = (REG_WRITE_DELAY + CELL_PROGRAM_DELAY) * 3;
//			expected_write_duration += REG_WRITE_DELAY + CELL_PROGRAM_DELAY - 100;
//			expected_write_duration += REG_WRITE_DELAY + CELL_PROGRAM_DELAY - 100;
//			expected_write_duration += REG_WRITE_DELAY + CELL_PROGRAM_DELAY - 100;
//			printf("expected_write_duration: %d\n", expected_write_duration);

			if( usec_diff_1 >= usec_diff_min && usec_diff_1 <= usec_diff_max
				&& usec_diff_2 >= usec_diff_min && usec_diff_2 <= usec_diff_max
				) {

				ASSERT_LE(expected_write_duration, SSD_TOTAL_WRITE_DELAY());
				ASSERT_GE(expected_write_duration + bw_delay_threshold(write_ops), SSD_TOTAL_WRITE_DELAY());

				break;
			}

		} while(1);
	}


	/**
	 * Case 28. erase after read. diff > switch read + reg read & diff < switch read + reg read + cell read
	 */
	TEST_P(SSDIoEmulatorUnitTest, BWCase28) {

		int flash_nb = 0;
		int block_nb = 0;
		int page_nb = 0;

		int usec_diff_min = 100;
		int usec_diff_max = 140;

		unsigned long int expected_write_duration = 0;
		unsigned long int expected_read_duration = 0;
		int write_ops = 1;
		int read_ops = 2;

		BLOCK_ERASE_DELAY = 2000;

		ASSERT_GT(usec_diff_min, CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY);
		ASSERT_LT(usec_diff_min, CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY + CELL_READ_DELAY);

		ASSERT_GT(usec_diff_max, CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY);
		ASSERT_LT(usec_diff_max, CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY + CELL_READ_DELAY);

		do {

			SSD_IO_INIT();

			int64_t current_timestamp = get_usec();

//			SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, IO_PAGE_FIRST);

			int64_t th1_started;
			int64_t th2_started;
			int64_t th3_started;

			// run SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, -1) method
			// at time: @(current_timestamp + 1000)
			pthread_t* th1 = run_ssd_page_write_thread(current_timestamp + 100000
					, &th1_started
					, flash_nb,block_nb,page_nb,0, WRITE, IO_PAGE_FIRST);
			// run SSD_BLOCK_ERASE(flash_nb,block_nb) method
			// at time: @(current_timestamp + 1000 + usec_diff)
			pthread_t* th2 = run_ssd_page_read_thread(current_timestamp + 100000 + usec_diff_min
					, &th2_started
					, flash_nb,block_nb,page_nb,0, READ, IO_PAGE_FIRST);
//			// run SSD_BLOCK_ERASE(flash_nb,block_nb) method
//			// at time: @(current_timestamp + 1000 + usec_diff)
			pthread_t* th3 = run_ssd_page_read_thread(current_timestamp + 100000 + usec_diff_min * 2
					, &th3_started
					, flash_nb,block_nb,page_nb,0, READ, IO_PAGE_FIRST);

			pthread_join(*th1, NULL);
			pthread_join(*th2, NULL);
			pthread_join(*th3, NULL);

			int usec_diff_1 = th2_started - th1_started;
			int usec_diff_2 = th3_started - th2_started;

//			printf("usec_diff_1: %d\n", usec_diff_1);
//			printf("usec_diff_2: %d\n", usec_diff_2);
			//
			// duration calculation:
			//
			// time=0
			// 		w1 start
			// time = usec_diff_1
			//		r1 wait, w1 left REG_WRITE_DELAY + CELL_PROGRAM_DELAY - usec_diff_1
			expected_read_duration += REG_WRITE_DELAY + CELL_PROGRAM_DELAY - usec_diff_1;
			// time = usec_diff_1 + usec_diff_2
			//		r2 wait, w1 left REG_WRITE_DELAY + CELL_PROGRAM_DELAY - usec_diff_1 - usec_diff_2
			expected_read_duration += REG_WRITE_DELAY + CELL_PROGRAM_DELAY - usec_diff_1 - usec_diff_2;
			// time = REG_WRITE_DELAY + CELL_PROGRAM_DELAY
			//		w1 done
			expected_write_duration = REG_WRITE_DELAY + CELL_PROGRAM_DELAY;
			//		w2 start
			//		w3 wait, w2 left CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY + CELL_READ_DELAY
			expected_read_duration += CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY + CELL_READ_DELAY;
			// time = (REG_WRITE_DELAY + CELL_PROGRAM_DELAY) * 2
			//		w2 done
			expected_read_duration += CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY + CELL_READ_DELAY;
			//		w3 start
			// time = (REG_WRITE_DELAY + CELL_PROGRAM_DELAY) * 3
			//		w3 done
			expected_read_duration +=  REG_READ_DELAY + CELL_READ_DELAY;

			if( usec_diff_1 >= usec_diff_min && usec_diff_1 <= usec_diff_max
				&& usec_diff_2 >= usec_diff_min && usec_diff_2 <= usec_diff_max
				) {

				ASSERT_LE(expected_write_duration, SSD_TOTAL_WRITE_DELAY());
				ASSERT_GE(expected_write_duration + bw_delay_threshold(write_ops), SSD_TOTAL_WRITE_DELAY());

				break;
			}

		} while(1);
	}


	/**
	 * Case 29. erase after read. diff > switch read + reg read & diff < switch read + reg read + cell read
	 */
	TEST_P(SSDIoEmulatorUnitTest, BWCase29) {

		int flash_nb = 0;
		int block_nb = 0;
		int page_nb = 0;

		int usec_diff_min = 100;
		int usec_diff_max = 140;

		unsigned long int expected_write_duration = 0;
		unsigned long int expected_read_duration = 0;
		int write_ops = 2;
		int read_ops = 2;

		BLOCK_ERASE_DELAY = 2000;

		ASSERT_GT(usec_diff_min, CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY);
		ASSERT_LT(usec_diff_min, CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY + CELL_READ_DELAY);

		ASSERT_GT(usec_diff_max, CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY);
		ASSERT_LT(usec_diff_max, CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY + CELL_READ_DELAY);

		do {

			SSD_IO_INIT();

			int64_t current_timestamp = get_usec();

//			SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, IO_PAGE_FIRST);

			int64_t th1_started;
			int64_t th2_started;
			int64_t th3_started;

			SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, -1);
			// run SSD_BLOCK_ERASE(flash_nb,block_nb) method
			// at time: @(current_timestamp + 1000 + usec_diff)
			pthread_t* th1 = run_ssd_page_read_thread(current_timestamp + 100000
					, &th1_started
					, flash_nb,block_nb,page_nb,0, READ, IO_PAGE_FIRST);
			// run SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, -1) method
			// at time: @(current_timestamp + 1000)
			pthread_t* th2 = run_ssd_page_write_thread(current_timestamp + 100000 + usec_diff_min
					, &th2_started
					, flash_nb,block_nb,page_nb+1,0, WRITE, IO_PAGE_FIRST);
//			// run SSD_BLOCK_ERASE(flash_nb,block_nb) method
//			// at time: @(current_timestamp + 1000 + usec_diff)
			pthread_t* th3 = run_ssd_page_read_thread(current_timestamp + 100000 + usec_diff_min * 2
					, &th3_started
					, flash_nb,block_nb,page_nb+1,0, READ, IO_PAGE_FIRST);

			pthread_join(*th1, NULL);
			pthread_join(*th2, NULL);
			pthread_join(*th3, NULL);

			int usec_diff_1 = th2_started - th1_started;
			int usec_diff_2 = th3_started - th2_started;


//			printf("usec_diff_1: %d\n", usec_diff_1);
//			printf("usec_diff_2: %d\n", usec_diff_2);
			//
			// duration calculation:
			//
			// time=0
			expected_write_duration = REG_WRITE_DELAY + CELL_PROGRAM_DELAY;
			// 		r1 start
			// time = usec_diff_1
			//		w1 wait, w1 left REG_WRITE_DELAY + CELL_PROGRAM_DELAY - usec_diff_1
			expected_write_duration += CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY + CELL_READ_DELAY - usec_diff_1;
			// time = usec_diff_1 + usec_diff_2
			//		r2 wait, w1 left REG_WRITE_DELAY + CELL_PROGRAM_DELAY - usec_diff_1 - usec_diff_2
			expected_read_duration += CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY + CELL_READ_DELAY - usec_diff_1 - usec_diff_2;
			// time = CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY + CELL_READ_DELAY
			//		r1 done
			expected_read_duration += CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY + CELL_READ_DELAY;
			//		w1 start
			//		r2 wait, w1 left CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY + CELL_READ_DELAY
			expected_read_duration += CHANNEL_SWITCH_DELAY_W + REG_WRITE_DELAY + CELL_PROGRAM_DELAY;
			// time = CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY + CELL_READ_DELAY
			//			+ CHANNEL_SWITCH_DELAY_W + REG_WRITE_DELAY + CELL_PROGRAM_DELAY
			//		w1 done
			expected_write_duration += CHANNEL_SWITCH_DELAY_W + REG_WRITE_DELAY + CELL_PROGRAM_DELAY;
			//		r2 start
			// time = CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY + CELL_READ_DELAY
			//			+ CHANNEL_SWITCH_DELAY_W + REG_WRITE_DELAY + CELL_PROGRAM_DELAY
			//			+ CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY + CELL_READ_DELAY
			//		r2 done
			expected_read_duration += CHANNEL_SWITCH_DELAY_W + REG_WRITE_DELAY + CELL_PROGRAM_DELAY;
			if( usec_diff_1 >= usec_diff_min && usec_diff_1 <= usec_diff_max
				&& usec_diff_2 >= usec_diff_min && usec_diff_2 <= usec_diff_max
				) {

				ASSERT_LE(expected_write_duration, SSD_TOTAL_WRITE_DELAY());
				ASSERT_GE(expected_write_duration + bw_delay_threshold(write_ops), SSD_TOTAL_WRITE_DELAY());

				break;
			}

		} while(1);
	}

    /***
     *
     * SSD statistic counters
     */
    /**
     *	case 1
     *	logical page write
     */
	TEST_P(SSDIoEmulatorUnitTest, CountersCase1) {

		int flash_nb = 0;
		int block_nb = 0;
		int page_nb = 0;
		int logica_page_writes = 1;
		int physical_page_writes = 1;

		SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, IO_PAGE_FIRST);
		ASSERT_EQ(physical_page_writes, SSD_GET_PHYSICAL_PAGE_WRITE_COUNT());
		ASSERT_EQ(logica_page_writes, SSD_GET_LOGICAL_PAGE_WRITE_COUNT());
	}
    /**
     *	case 2
     *	physical page write
     */
	TEST_P(SSDIoEmulatorUnitTest, CountersCase2) {

		int flash_nb = 0;
		int block_nb = 0;
		int page_nb = 0;

		int logica_page_writes = 0;
		int physical_page_writes = 1;

		SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, GC_WRITE, IO_PAGE_FIRST);
		ASSERT_EQ(physical_page_writes, SSD_GET_PHYSICAL_PAGE_WRITE_COUNT());
		ASSERT_EQ(logica_page_writes, SSD_GET_LOGICAL_PAGE_WRITE_COUNT());
	}

	/**
	 * case 3
	 * logical page read
	 */
	TEST_P(SSDIoEmulatorUnitTest, CountersCase3) {

		int flash_nb = 0;
		int block_nb = 0;
		int page_nb = 0;

		int logica_page_reads = 1;
		int physical_page_reads = 1;

		SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, IO_PAGE_FIRST);
		SSD_PAGE_READ(flash_nb,block_nb,page_nb,0, READ, IO_PAGE_FIRST);

		ASSERT_EQ(physical_page_reads, SSD_GET_PHYSICAL_PAGE_READ_COUNT());
		ASSERT_EQ(logica_page_reads, SSD_GET_LOGICAL_PAGE_READ_COUNT());
	}
	/**
	 * case 4
	 * non-logical page read
	 */
	TEST_P(SSDIoEmulatorUnitTest, CountersCase4) {

		int flash_nb = 0;
		int block_nb = 0;
		int page_nb = 0;

		int logica_page_reads = 0;
		int physical_page_reads = 1;

		SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, IO_PAGE_FIRST);
		SSD_PAGE_READ(flash_nb,block_nb,page_nb,0, GC_READ, IO_PAGE_FIRST);

		ASSERT_EQ(physical_page_reads, SSD_GET_PHYSICAL_PAGE_READ_COUNT());
		ASSERT_EQ(logica_page_reads, SSD_GET_LOGICAL_PAGE_READ_COUNT());
	}

	/**
	 * case 5
	 * erase
	 */
	TEST_P(SSDIoEmulatorUnitTest, CountersCase5) {

		int flash_nb = 0;
		int block_nb = 0;
		int page_nb = 0;

		int erases = 1;

		SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, IO_PAGE_FIRST);
		SSD_BLOCK_ERASE(flash_nb,block_nb);
		ASSERT_EQ(erases, SSD_GET_ERASE_COUNT());
	}

	/***
	 * SSD utils
	 */
	/**
	 * case 1
	 * logical page write
	 */
	TEST_P(SSDIoEmulatorUnitTest, SSDUtilCase1) {
		int flash_nb = 0;
		int block_nb = 0;
		int page_nb = 0;
		int occupied_pages = 1;
		SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, IO_PAGE_FIRST);
		ASSERT_EQ((double)occupied_pages / PAGES_IN_SSD, SSD_UTIL_CALC());
	}
	/**
	 * case 2
	 * non-logical page write
	 */
	TEST_P(SSDIoEmulatorUnitTest, SSDUtilCase2) {
		int flash_nb = 0;
		int block_nb = 0;
		int page_nb = 0;
		int occupied_pages = 1;
		SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, GC_WRITE, IO_PAGE_FIRST);
		ASSERT_EQ((double)occupied_pages / PAGES_IN_SSD, SSD_UTIL_CALC());
	}

	/**
	 * case 3
	 * erase 1 of 1 blocks
	 */
	TEST_P(SSDIoEmulatorUnitTest, SSDUtilCase3) {
		int flash_nb = 0;
		int block_nb = 0;
		int page_nb = 0;
		int occupied_pages = 0;
		SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, IO_PAGE_FIRST);
		SSD_BLOCK_ERASE(flash_nb,block_nb);
		ASSERT_EQ((double)occupied_pages / PAGES_IN_SSD, SSD_UTIL_CALC());
	}

	/**
	 * case 4
	 * erase 1 of 1 blocks
	 */
	TEST_P(SSDIoEmulatorUnitTest, SSDUtilCase4) {
		int flash_nb = 0;
		int block_nb = 0;
		int page_nb = 0;
		int occupied_pages = 0;
		SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, IO_PAGE_FIRST);
		SSD_PAGE_WRITE(flash_nb,block_nb+1,page_nb,0, WRITE, IO_PAGE_FIRST);
		occupied_pages = 2;
		ASSERT_EQ((double)occupied_pages / PAGES_IN_SSD, SSD_UTIL_CALC());
		SSD_BLOCK_ERASE(flash_nb,block_nb);
		occupied_pages--;
		ASSERT_EQ((double)occupied_pages / PAGES_IN_SSD, SSD_UTIL_CALC());
	}


	/***
	 * Operations success / failure indication
	 */

	/**
	 * Success write status
	 * on single page write
	 */
	/**
	 * case 1
	 */
	TEST_P(SSDIoEmulatorUnitTest, ReturnedValueCase1) {
		int flash_nb = 0;
		int block_nb = 0;
		int page_nb = 0;
		ASSERT_EQ(FTL_SUCCESS, SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, IO_PAGE_FIRST));
	}

	/**
	 * case 2
	 */
//	// TODO un-comment on write validation fix
//	TEST_P(SSDIoEmulatorUnitTest, ReturnedValueCase2) {
//		int flash_nb = 0;
//		int block_nb = 0;
//		int page_nb = 0;
//		ASSERT_EQ(FTL_SUCCESS, SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, IO_PAGE_FIRST));
//		ASSERT_EQ(FTL_FAILURE, SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, IO_PAGE_FIRST));
//	}

	/**
	 * case 3
	 */
	TEST_P(SSDIoEmulatorUnitTest, ReturnedValueCase3) {
		int flash_nb = 0;
		int block_nb = 0;
		int page_nb = 0;
		ASSERT_EQ(FTL_SUCCESS, SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, IO_PAGE_FIRST));
		ASSERT_EQ(FTL_SUCCESS, SSD_PAGE_READ(flash_nb,block_nb,page_nb,0, READ, IO_PAGE_FIRST));
	}
	/**
	 * case 4
	 */
//	// TODO un-comment on write validation fix
//	TEST_P(SSDIoEmulatorUnitTest, ReturnedValueCase4) {
//		int flash_nb = 0;
//		int block_nb = 0;
//		int page_nb = 0;
//		ASSERT_EQ(FTL_FAILURE, SSD_PAGE_READ(flash_nb,block_nb,page_nb,0, READ, IO_PAGE_FIRST));
//	}


	/**
	 * case 5
	 */
	TEST_P(SSDIoEmulatorUnitTest, ReturnedValueCase5) {
		int flash_nb = FLASH_NB;
		int block_nb = 0;
		int page_nb = 0;
		ASSERT_EQ(FTL_FAILURE, SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, IO_PAGE_FIRST));
	}
	/**
	 * case 6
	 */
	TEST_P(SSDIoEmulatorUnitTest, ReturnedValueCase6) {
		int flash_nb = 0;
		int block_nb = BLOCK_NB;
		int page_nb = 0;
		ASSERT_EQ(FTL_FAILURE, SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, IO_PAGE_FIRST));
	}
	/**
	 * case 7
	 */
	TEST_P(SSDIoEmulatorUnitTest, ReturnedValueCase7) {
		int flash_nb = 0;
		int block_nb = 0;
		int page_nb = PAGE_NB;
		ASSERT_EQ(FTL_FAILURE, SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, IO_PAGE_FIRST));
	}
	/**
	 * case 8
	 */
	TEST_P(SSDIoEmulatorUnitTest, ReturnedValueCase8) {
		int flash_nb = FLASH_NB;
		int block_nb = 0;
		int page_nb = 0;
		ASSERT_EQ(FTL_FAILURE, SSD_PAGE_READ(flash_nb,block_nb,page_nb,0, READ, IO_PAGE_FIRST));
	}
	/**
	 * case 9
	 */
	TEST_P(SSDIoEmulatorUnitTest, ReturnedValueCase9) {
		int flash_nb = 0;
		int block_nb = BLOCK_NB;
		int page_nb = 0;
		ASSERT_EQ(FTL_FAILURE, SSD_PAGE_READ(flash_nb,block_nb,page_nb,0, READ, IO_PAGE_FIRST));
	}
	/**
	 * case 10
	 */
	TEST_P(SSDIoEmulatorUnitTest, ReturnedValueCase10) {
		int flash_nb = 0;
		int block_nb = 0;
		int page_nb = PAGE_NB;
		ASSERT_EQ(FTL_FAILURE, SSD_PAGE_READ(flash_nb,block_nb,page_nb,0, READ, IO_PAGE_FIRST));
	}

	/**
	 * case 11
	 */
	// TODO un-comment on write validation fix
//	TEST_P(SSDIoEmulatorUnitTest, ReturnedValueCase11) {
//		int flash_nb = 0;
//		int block_nb = 0;
//		int page_nb = 0;
//		ASSERT_EQ(FTL_SUCCESS, SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, IO_PAGE_FIRST));
//		ASSERT_EQ(FTL_SUCCESS, SSD_BLOCK_ERASE(flash_nb,block_nb));
//		ASSERT_EQ(FTL_FAILURE, SSD_PAGE_READ(flash_nb,block_nb,page_nb,0, READ, IO_PAGE_FIRST));
//	}

	/**
	 * case 12
	 */
	TEST_P(SSDIoEmulatorUnitTest, ReturnedValueCase12) {
		int flash_nb = 0;
		int block_nb = 0;
		ASSERT_EQ(FTL_SUCCESS, SSD_BLOCK_ERASE(flash_nb,block_nb));
	}

	/**
	 * case 13
	 */
	TEST_P(SSDIoEmulatorUnitTest, ReturnedValueCase13) {
		int flash_nb = 0;
		int block_nb = 0;
		int page_nb = 0;
		ASSERT_EQ(FTL_SUCCESS, SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, IO_PAGE_FIRST));
		ASSERT_EQ(FTL_SUCCESS, SSD_BLOCK_ERASE(flash_nb,block_nb));
		ASSERT_EQ(FTL_SUCCESS, SSD_PAGE_WRITE(flash_nb,block_nb,page_nb,0, WRITE, IO_PAGE_FIRST));
	}





} //namespace
