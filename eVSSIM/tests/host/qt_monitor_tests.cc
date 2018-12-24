
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

pthread_t listener_thread;
void* monitor_listener(void *arg) {

	usleep(50000);
    int sockfd = 0, n = 0;
    char recvBuff[1024];
    struct sockaddr_in serv_addr;

    memset(recvBuff, '0',sizeof(recvBuff));
    if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        printf("\n Error : Could not create socket \n");
    }

    memset(&serv_addr, '0', sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(9999);

    char* host = "127.0.0.1";
    if(inet_pton(AF_INET, host, &serv_addr.sin_addr)<=0)
    {
        printf("\n inet_pton error occured\n");
    }

    if( connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
       printf("\n Error : Connect Failed \n");
       printf("errno : '%d' \n", errno);
    }

    while ( (n = read(sockfd, recvBuff, sizeof(recvBuff)-1)) > 0)
    {
        recvBuff[n] = 0;
		char * pch;
         pch = strtok (recvBuff,"\n");
         while (pch != NULL)
         {
		   PARSE_LOG(pch, &log_monitor);
           pch = strtok (NULL, "\n");
         }
    }

    if(n < 0)
    {
        printf("\n Read error \n");
    }
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

    class QTMonitorUnitTest : public ::testing::TestWithParam<std::pair<size_t,size_t> > {
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

                int err;
                err = pthread_create(&listener_thread, NULL, &monitor_listener, NULL);
                if (err != 0) printf("\ncan't create thread :[%s]", strerror(err));

			INIT_LOGGER_MONITOR(&log_monitor);
                SET_MONITOR(MONITOR_CUSTOM);
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

    std::vector<std::pair<size_t,size_t> > GetParams() {
        std::vector<std::pair<size_t,size_t> > list;
        const int constFlashNum = DEFAULT_FLASH_NB;
        unsigned int i = 0;
        list.push_back(std::make_pair(parameters::Allsizemb[i], constFlashNum ));
        return list;
    }

    INSTANTIATE_TEST_CASE_P(DiskSize, QTMonitorUnitTest, ::testing::ValuesIn(GetParams()));
	TEST_P(QTMonitorUnitTest, NoGCNoMergeWriteAmpCalculate) {
		SSD_PAGE_WRITE(0,0,0,0, WRITE, -1);
		usleep(100000);
		ASSERT_EQ(1, log_monitor.written_page);
		ASSERT_EQ(1.0, log_monitor.write_amplification);
	}

	TEST_P(QTMonitorUnitTest, GCWriteAmpCalculate) {
		SSD_PAGE_WRITE(0,0,0,0, WRITE, -1);
		SSD_PAGE_WRITE(0,0,0,0, GC_WRITE, -1);
		usleep(100000);
		ASSERT_EQ(1, log_monitor.written_page);
		ASSERT_EQ(2.0, log_monitor.write_amplification);
	}

	TEST_P(QTMonitorUnitTest, VariantWritesAmpCalculate) {
		int i;
		int logical_writes = 40;
		int physical_writes = 50;

		for( i = 0; i < logical_writes; i++ )
			SSD_PAGE_WRITE(0,0,0,0, WRITE, -1);

		for( i = 0; i < physical_writes - logical_writes; i++ )
			SSD_PAGE_WRITE(0,0,0,0, GC_WRITE, -1);

		usleep(100000);
		ASSERT_EQ(logical_writes, log_monitor.written_page);
		ASSERT_EQ((double)physical_writes / logical_writes, log_monitor.write_amplification);
	}

	TEST_P(QTMonitorUnitTest, CountEraseBlockOps) {
		SSD_BLOCK_ERASE(0,0);
		usleep(100000);
		ASSERT_EQ(1, log_monitor.erase_count);
	}

	TEST_P(QTMonitorUnitTest, CountWriteSector) {
		_FTL_WRITE_SECT(0, 1);
		usleep(100000);
		ASSERT_EQ(1, log_monitor.write_count);
		ASSERT_EQ(1, log_monitor.write_sector_count);
	}

	TEST_P(QTMonitorUnitTest, CustomLengthCountWriteSector) {
		_FTL_WRITE_SECT(0, 16);
		usleep(100000);
		ASSERT_EQ(1, log_monitor.write_count);
		ASSERT_EQ(16, log_monitor.write_sector_count);
	}

	TEST_P(QTMonitorUnitTest, CountReadSector) {
		_FTL_WRITE_SECT(0, 1);
		_FTL_READ_SECT(0, 1);
		usleep(100000);
		ASSERT_EQ(1, log_monitor.read_count);
		ASSERT_EQ(1, log_monitor.read_sector_count);
	}

	TEST_P(QTMonitorUnitTest, CustomLengthCountReadSector) {
		_FTL_WRITE_SECT(0, 16);
		_FTL_READ_SECT(0, 16);
		usleep(100000);
		ASSERT_EQ(1, log_monitor.read_count);
		ASSERT_EQ(16, log_monitor.read_sector_count);
	}
} //namespace
