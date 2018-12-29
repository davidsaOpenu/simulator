
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

#define BW_EPSILON 0.2

typedef struct {
	int64_t timestamp;
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
	int64_t sl = args_struct->timestamp - get_usec() - 200;
	if( sl > 0 )
		usleep(sl);
	_SSD_BUSY_WAIT(args_struct->timestamp);
	int res = SSD_PAGE_WRITE(args_struct->flash_nb
			, args_struct->block_nb
			, args_struct->page_nb
			, args_struct->offset
			, args_struct->type
			, args_struct->io_page_nb);
	return (void *)&res;
}


void* ssd_read_thread(void* args)
{
	ssd_args_struct* args_struct = (ssd_args_struct*)args;
	int64_t sl = args_struct->timestamp - get_usec() - 200;
	if( sl > 0 )
		usleep(sl);
	_SSD_BUSY_WAIT(args_struct->timestamp);
	int res = SSD_PAGE_READ(args_struct->flash_nb
			, args_struct->block_nb
			, args_struct->page_nb
			, args_struct->offset
			, args_struct->type
			, args_struct->io_page_nb);
	return (void *)&res;
}


int run_ssd_page_write_thread(int64_t timestamp
		, unsigned int flash_nb
		, unsigned int block_nb
		, unsigned int page_nb
		, int offset
		, int type
		, int io_page_nb) {

    pthread_t th;

    ssd_args_struct* args = (ssd_args_struct*)malloc(sizeof(ssd_args_struct));
    args->timestamp = timestamp;
    args->flash_nb = flash_nb;
    args->block_nb = block_nb;
    args->page_nb = page_nb;
    args->offset = offset;
    args->type = type;
    args->io_page_nb = io_page_nb;
    return pthread_create(&th, NULL, &ssd_write_thread, (void *)args);
}


int run_ssd_page_read_thread(int64_t timestamp
		, unsigned int flash_nb
		, unsigned int block_nb
		, unsigned int page_nb
		, int offset
		, int type
		, int io_page_nb) {

    pthread_t th;

    ssd_args_struct* args = (ssd_args_struct*)malloc(sizeof(ssd_args_struct));
    args->timestamp = timestamp;
    args->flash_nb = flash_nb;
    args->block_nb = block_nb;
    args->page_nb = page_nb;
    args->offset = offset;
    args->type = type;
    args->io_page_nb = io_page_nb;
    return pthread_create(&th, NULL, &ssd_read_thread, (void *)args);
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

void sync_monitor() {
	SEND_TO_PERF_CHECKER(WRITE, 0, LATENCY_OP);
	SEND_TO_PERF_CHECKER(READ, 0, LATENCY_OP);
	usleep(100000);
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
                    "BLOCK_ERASE_DELAY 2000\n"
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
     *  Count logical & pysical writes
     *  on single logical page write
     */
	TEST_P(SSDIoEmulatorUnitTest, NoGCNoMergeWriteAmpCalculate) {

		int flash_nb = 0;
		int block_nb = 0;

		SSD_PAGE_WRITE(flash_nb,block_nb,0,0, WRITE, -1);
		ASSERT_EQ(1, SSD_GET_PHYSICAL_PAGE_WRITE_COUNT());
		ASSERT_EQ(1, SSD_GET_LOGICAL_PAGE_WRITE_COUNT());
	}

    /**
     *  Count logical & pysical writes
     *  on logical & physical page write
     */
	TEST_P(SSDIoEmulatorUnitTest, GCWriteAmpCalculate) {
		SSD_PAGE_WRITE(0,0,0,0, WRITE, -1);
		SSD_PAGE_WRITE(0,0,1,0, GC_WRITE, -1);
		ASSERT_EQ(2, SSD_GET_PHYSICAL_PAGE_WRITE_COUNT());
		ASSERT_EQ(1, SSD_GET_LOGICAL_PAGE_WRITE_COUNT());
		ASSERT_EQ(0, SSD_GET_LOGICAL_PAGE_READ_COUNT());
		ASSERT_EQ(0, SSD_GET_PHYSICAL_PAGE_READ_COUNT());
	}

    /**
     *  Count logical & pysical writes
     *  on mixed logical & physical writes
     */
	TEST_P(SSDIoEmulatorUnitTest, VariantWritesAmpCalculate) {
		int i;
		int logical_writes = 4;
		int gc_writes = 5;

		for( i = 0; i < logical_writes; i++ )
			SSD_PAGE_WRITE(0,0,i,0, WRITE, -1);

		for( i = 0; i < gc_writes; i++ )
			SSD_PAGE_WRITE(1,0,i,0, GC_WRITE, -1);

		ASSERT_EQ(logical_writes + gc_writes, SSD_GET_PHYSICAL_PAGE_WRITE_COUNT());
		ASSERT_EQ(logical_writes, SSD_GET_LOGICAL_PAGE_WRITE_COUNT());
	}

	/**
	 * Count erase
	 * on single erase op
	 */
	TEST_P(SSDIoEmulatorUnitTest, CountEraseBlockOps) {
		SSD_BLOCK_ERASE(0,0);
		ASSERT_EQ(1, SSD_GET_ERASE_COUNT());
	}

	/**
	 * SSD Utils calculation
	 * on single page write
	 */
	TEST_P(SSDIoEmulatorUnitTest, SSDUtilSingleWrite) {
		SSD_PAGE_WRITE(0,0,0,0, WRITE, -1);
		ASSERT_EQ((double)1 / PAGES_IN_SSD, SSD_UTIL_CALC());
	}

	/**
	 * SSD Utils calculation
	 * on whole block write
	 */
	TEST_P(SSDIoEmulatorUnitTest, SSDUtilWholeBlockWrite) {
		int i, j;
		for( i = 0; i < BLOCK_NB; i++ )
			for( j = 0; j < PAGE_NB; j++ )
				SSD_PAGE_WRITE(0,i,j,0, WRITE, -1);
		ASSERT_EQ((double)1 / FLASH_NB, SSD_UTIL_CALC());
	}

	/**
	 * SSD Utils calculation
	 * on multiple blocks
	 */
	TEST_P(SSDIoEmulatorUnitTest, SSDUtilMultiBlockWrite) {
		int i;
		int b1_writes = 3;
		int b2_writes = 6;
		for( i = 0; i < b1_writes; i++ )
			SSD_PAGE_WRITE(0,0,i,0, WRITE, -1);
		for( i = 0; i < b2_writes; i++ )
			SSD_PAGE_WRITE(0,1,i,0, WRITE, -1);
		ASSERT_EQ((double)(b1_writes + b2_writes) / PAGES_IN_SSD, SSD_UTIL_CALC());
	}

	/**
	 * SSD Utils calculation
	 * on multiple flash writes
	 */
	TEST_P(SSDIoEmulatorUnitTest, SSDUtilMultiFlashWrite) {
		int i;
		int b1_writes = 3;
		int b2_writes = 6;
		for( i = 0; i < b1_writes; i++ )
			SSD_PAGE_WRITE(0,0,i,0, WRITE, -1);
		for( i = 0; i < b2_writes; i++ )
			SSD_PAGE_WRITE(1,0,i,0, WRITE, -1);
		ASSERT_EQ((double)(b1_writes + b2_writes) / PAGES_IN_SSD, SSD_UTIL_CALC());
	}

	/**
	 * Success write status
	 * on single page write
	 */
	TEST_P(SSDIoEmulatorUnitTest, SuccessWriteOnSingleWrite) {
		ASSERT_EQ(FTL_SUCCESS, SSD_PAGE_WRITE(0,0,0,0, WRITE, -1));
	}


	/**
	 * Success write status
	 * on multiple pages write
	 */
	TEST_P(SSDIoEmulatorUnitTest, SuccessWriteOnMultipleWrite) {
		ASSERT_EQ(FTL_SUCCESS, SSD_PAGE_WRITE(0,0,0,0, WRITE, -1));
		ASSERT_EQ(FTL_SUCCESS, SSD_PAGE_WRITE(0,0,1,0, WRITE, -1));
	}

	/**
	 * Success write status
	 * on multiple block writes
	 */
	TEST_P(SSDIoEmulatorUnitTest, SuccessWriteOnMultipleBlockWrite) {
		ASSERT_EQ(FTL_SUCCESS, SSD_PAGE_WRITE(0,0,0,0, WRITE, -1));
		ASSERT_EQ(FTL_SUCCESS, SSD_PAGE_WRITE(0,1,0,0, WRITE, -1));
	}

	/**
	 * Success write status
	 * on multiple flash writes
	 */
	TEST_P(SSDIoEmulatorUnitTest, SuccessWriteOnMultipleFlashWrite) {
		ASSERT_EQ(FTL_SUCCESS, SSD_PAGE_WRITE(0,0,0,0, WRITE, -1));
		ASSERT_EQ(FTL_SUCCESS, SSD_PAGE_WRITE(1,0,0,0, WRITE, -1));
	}

	/**
	 * Failure write status
	 * on override page write attempt
	 */
	// TODO un-comment on write validation fix
//	TEST_P(SSDIoEmulatorUnitTest, FailureWriteOnOverrideWrite) {
//		ASSERT_EQ(FTL_SUCCESS, SSD_PAGE_WRITE(0,0,0,0, WRITE, -1));
//		ASSERT_EQ(FTL_FAILURE, SSD_PAGE_WRITE(0,0,0,0, WRITE, -1));
//	}

	/**
	 * Success write status
	 * on erase and re-write
	 */
	TEST_P(SSDIoEmulatorUnitTest, SuccessWriteOnEraseAndReWrite) {
		ASSERT_EQ(FTL_SUCCESS, SSD_PAGE_WRITE(0,0,0,0, WRITE, -1));
		ASSERT_EQ(FTL_SUCCESS, SSD_BLOCK_ERASE(0,0));
		ASSERT_EQ(FTL_SUCCESS, SSD_PAGE_WRITE(0,0,0,0, WRITE, -1));
	}

	/**
	 * Success read status
	 * on single page read
	 */
	TEST_P(SSDIoEmulatorUnitTest, SuccessReadOnSingleWrite) {
		ASSERT_EQ(FTL_SUCCESS, SSD_PAGE_WRITE(0,0,0,0, WRITE, -1));
		ASSERT_EQ(FTL_SUCCESS, SSD_PAGE_READ(0,0,0,0, READ, -1));
	}

	/**
	 * Failure read status
	 * on read non-written page
	 */
	// TODO un-comment on write validation fix
//	TEST_P(SSDIoEmulatorUnitTest, FailureReadOnNonWrittenPage) {
//		ASSERT_EQ(FTL_FAILURE, SSD_PAGE_READ(0,0,0,0, READ, -1));
//	}

	/**
	 * Failure write / read status
	 * on write / read non exists page
	 */
	TEST_P(SSDIoEmulatorUnitTest, FailureWriteReadOnNonExistsComponenets) {
		// page number @PAGE_NB is not exists
		ASSERT_EQ(FTL_FAILURE, SSD_PAGE_WRITE(0,0,PAGE_NB,0, WRITE, -1));
		ASSERT_EQ(FTL_FAILURE, SSD_PAGE_READ(0,0,PAGE_NB,0, READ, -1));

		// block number @BLOCK_NB is not exists
		ASSERT_EQ(FTL_FAILURE, SSD_PAGE_WRITE(0,BLOCK_NB,0,0, WRITE, -1));
		ASSERT_EQ(FTL_FAILURE, SSD_PAGE_READ(0,BLOCK_NB,0,0, READ, -1));

		// flash number @FLASH_NB is not exists
		ASSERT_EQ(FTL_FAILURE, SSD_PAGE_WRITE(FLASH_NB,0,0,0, WRITE, -1));
		ASSERT_EQ(FTL_FAILURE, SSD_PAGE_READ(FLASH_NB,0,0,0, READ, -1));
	}

	/**
	 * Write bandwidth
	 * on single page write
	 */
	TEST_P(SSDIoEmulatorUnitTest, WriteBWAndDurationOnSingleWrite) {
		SSD_PAGE_WRITE(0,0,0,0, WRITE, -1);
		//
		// expected duration calculation:
		// 1st page write:
		//		after write register: duration is REG_WRITE_DELAY
		//		after cell programming: duration increased by CELL_PROGRAM_DELAY
		unsigned long int exp_duration = REG_WRITE_DELAY + CELL_PROGRAM_DELAY;
		ASSERT_GE(GET_IO_BANDWIDTH((double)(exp_duration - 3) / 1), SSD_WRITE_BANDWIDTH());
		ASSERT_LE(GET_IO_BANDWIDTH((double)(exp_duration + 3) / 1), SSD_WRITE_BANDWIDTH());
	}

	/**
	 * Write bandwidth
	 * on single two page write
	 * without io overhead
	 */
	TEST_P(SSDIoEmulatorUnitTest, WriteBWAndDurationOnTwoWriteNoIoOverhead) {

		unsigned long int exp_duration;

		SSD_PAGE_WRITE(0,0,0,0, WRITE, -1);
		SSD_PAGE_WRITE(0,0,1,0, WRITE, -1);

		//
		// duration calculation:
		//
		// time=0
		//		1st register write start
		exp_duration = 0;
		// time=@REG_WRITE_DELAY
		//		1st register write done
		exp_duration += REG_WRITE_DELAY;
		//		1st cell programming start
		// time+= @CELL_PROGRAM_DELAY
		//		1st cell programming done
		exp_duration += CELL_PROGRAM_DELAY;
		//		2nd register write start
		// time+=@REG_WRITE_DELAY
		//		2nd register write done
		exp_duration += REG_WRITE_DELAY;
		//		2nd read programming start
		// time+=@CELL_PROGRAM_DELAY
		//		2nd read programming done
		exp_duration += CELL_PROGRAM_DELAY;

		ASSERT_GE(GET_IO_BANDWIDTH((double)(exp_duration - 3) / 2), SSD_WRITE_BANDWIDTH());
		ASSERT_LE(GET_IO_BANDWIDTH((double)(exp_duration + 3) / 2), SSD_WRITE_BANDWIDTH());
	}

	/**
	 * Write / Read bandwidth
	 * on page write / read switching
	 */
	TEST_P(SSDIoEmulatorUnitTest, BWOnSwitchWriteCase1) {

		unsigned long int exp_write_duration, exp_read_duration;
		SSD_PAGE_WRITE(0,0,0,0, WRITE, -1);
		SSD_PAGE_READ(0,0,0,0, READ, -1);	// cause switch read delay
		SSD_PAGE_WRITE(0,0,2,0, WRITE, -1); // cause switch write delay

		//
		// write duration calculation:
		//
		// time=0
		//		1st register write start
		exp_write_duration = 0;
		exp_read_duration = 0;
		// time=@REG_WRITE_DELAY
		//		1st register write done
		exp_write_duration += REG_WRITE_DELAY;
		//		1st cell programming start
		// time+= @CELL_PROGRAM_DELAY
		//		1st cell programming done
		exp_write_duration += CELL_PROGRAM_DELAY;
		//		1st read channel switch start
		// time+= @CHANNEL_SWITCH_DELAY_W
		//		1st read channel switch done
		exp_read_duration += CHANNEL_SWITCH_DELAY_R;
		//		1st register read start
		// time+=@REG_READ_DELAY
		//		1st register read done
		exp_read_duration += REG_READ_DELAY;
		//		1st read programming start
		// time+=@CELL_READ_DELAY
		//		1st read programming done
		exp_read_duration += CELL_READ_DELAY;
		//		2nd write channel switch start
		// time+=@CHANNEL_SWITCH_DELAY_W
		//		2nd write channel switch done
		exp_write_duration += CHANNEL_SWITCH_DELAY_W;
		//		2nd register write start
		// time+=@REG_WRITE_DELAY
		//		2nd register write done
		exp_write_duration += REG_WRITE_DELAY;
		//		2nd cell programming start
		// time+=@CELL_PROGRAM_DELAY
		//		2nd cell programming done
		exp_write_duration += CELL_PROGRAM_DELAY;

		ASSERT_GE(GET_IO_BANDWIDTH((double)(exp_write_duration - 3) / 2), SSD_WRITE_BANDWIDTH());
		ASSERT_LE(GET_IO_BANDWIDTH((double)(exp_write_duration + 3) / 2), SSD_WRITE_BANDWIDTH());

		ASSERT_GE(GET_IO_BANDWIDTH((double)(exp_read_duration - 3) / 1), SSD_READ_BANDWIDTH());
		ASSERT_LE(GET_IO_BANDWIDTH((double)(exp_read_duration + 3) / 1), SSD_READ_BANDWIDTH());
	}

	/**
	 * Write bandwidth
	 * on two page write
	 * with io overhead on register & cell programming
	 */
	TEST_P(SSDIoEmulatorUnitTest, WriteBWAndDurationOnTwoWriteWithIoOverheadCase1) {

		int64_t current_timestamp = get_usec();
		int usec_diff = 20;
		unsigned long int exp_duration = 0;

		// validate @diff < @REG_WRITE_DELAY
		ASSERT_LT(usec_diff, REG_WRITE_DELAY);
		//
		// duration calculation:
		//
		// time=0
		//		1st register write start
		exp_duration = 0;
		// time=@usec_diff
		//		1st register write left @REG_WRITE_DELAY - @usec_diff
		//		2nd wait
		exp_duration += REG_WRITE_DELAY - usec_diff;
		// time=@REG_WRITE_DELAY
		//		1st register write done
		exp_duration += REG_WRITE_DELAY;
		//		2nd register write start
		//		1st cell programming start
		// time=@REG_WRITE_DELAY + @REG_WRITE_DELAY
		//		2nd write register done
		exp_duration += REG_WRITE_DELAY;
		//		1st cell programming left @CELL_PROGRAM_DELAY - @REG_WRITE_DELAY
		//		2nd cell programming wait
		exp_duration += CELL_PROGRAM_DELAY - REG_WRITE_DELAY;
		// time=@REG_WRITE_DELAY + @CELL_PROGRAM_DELAY
		//		1st cell programming done
		exp_duration += CELL_PROGRAM_DELAY;
		//		2nd cell programming start
		// time=@REG_WRITE_DELAY + @CELL_PROGRAM_DELAY * 2
		//		2nd cell programming done
		exp_duration += CELL_PROGRAM_DELAY;

		// run SSD_PAGE_WRITE(0,0,0,0, WRITE, -1) method
		// at time: @(current_timestamp + 1000)
		run_ssd_page_write_thread(current_timestamp + 1000
				, 0,0,0,0, WRITE, -1);
		// run SSD_PAGE_WRITE(0,0,1,0, WRITE, -1) method
		// at time: @(current_timestamp + 1000 + usec_diff)
		run_ssd_page_write_thread(current_timestamp + 1000 + usec_diff
				, 0,0,1,0, WRITE, -1);

		usleep(500000);
		ASSERT_GE(GET_IO_BANDWIDTH((double)(exp_duration - 8) / 2), SSD_WRITE_BANDWIDTH());
		ASSERT_LE(GET_IO_BANDWIDTH((double)(exp_duration + 8) / 2), SSD_WRITE_BANDWIDTH());
	}

	/**
	 * Write bandwidth
	 * on two page write
	 * with io overhead on cell programming only
	 */
	TEST_P(SSDIoEmulatorUnitTest, WriteBWAndDurationOnTwoWriteWithIoOverheadCase2) {

		int64_t current_timestamp = get_usec();
		int usec_diff = 200;
		unsigned long int exp_duration = 0;

		// validate @usec_diff >= @REG_WRITE_DELAY
		ASSERT_GE(usec_diff, REG_WRITE_DELAY);
		// validate @usec_diff + @REG_WRITE_DELAY  < @CELL_PROGRAM_DELAY
		ASSERT_LT(usec_diff + REG_WRITE_DELAY, CELL_PROGRAM_DELAY);
		//
		// duration calculation:
		//
		// time=0
		//		1st register write start
		exp_duration = 0;
		// time=@REG_WRITE_DELAY
		//		1st register write done
		exp_duration += REG_WRITE_DELAY;
		//		1st cell programming start
		// time=@usec_diff
		//		2nd register write start
		// time=@usec_diff + @REG_WRITE_DELAY
		//		2nd register write done
		exp_duration += REG_WRITE_DELAY;
		//		1st cell program left @CELL_PROGRAM_DELAY - @usec_diff
		//		2n wait
		exp_duration += CELL_PROGRAM_DELAY - usec_diff;
		// time=@REG_WRITE_DELAY + @CELL_PROGRAM_DELAY
		//		1st cell program done
		exp_duration += CELL_PROGRAM_DELAY;
		//		2nd cell program start
		// time=@REG_WRITE_DELAY + @CELL_PROGRAM_DELAY * 2
		//		2nd cell program done
		exp_duration += CELL_PROGRAM_DELAY;

		// run SSD_PAGE_WRITE(0,0,0,0, WRITE, -1) method
		// at time: @(current_timestamp + 1000)
		run_ssd_page_write_thread(current_timestamp + 1000
				, 0,0,0,0, WRITE, -1);
		// run SSD_PAGE_WRITE(0,0,1,0, WRITE, -1) method
		// at time: @(current_timestamp + 1000 + usec_diff)
		run_ssd_page_write_thread(current_timestamp + 1000 + usec_diff
				, 0,0,1,0, WRITE, -1);

		usleep(500000);
		printf("exp_duration: %ld\n", exp_duration);
		ASSERT_GE(GET_IO_BANDWIDTH((double)(exp_duration - 3) / 2), SSD_WRITE_BANDWIDTH());
		ASSERT_LE(GET_IO_BANDWIDTH((double)(exp_duration + 3) / 2), SSD_WRITE_BANDWIDTH());
	}


} //namespace
