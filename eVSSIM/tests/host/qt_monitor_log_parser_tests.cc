
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
                if (err != 0)
				printf("\ncan't create thread :[%s]", strerror(err));

			INIT_LOGGER_MONITOR(&log_monitor);
          		THREAD_SERVER();
            }
            virtual void TearDown() {
                FTL_TERM();
            	close(servSock);
            	close(clientSock);
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
    TEST_P(QTMonitorUnitTest, LogWritePageOnePage) {
    	SEND_LOG(clientSock, "WRITE PAGE 1");
        usleep(100000);
        ASSERT_EQ(1, log_monitor.write_count);
    }

    TEST_P(QTMonitorUnitTest, CumulativeLogWritePageOnePageMutipleTime) {
	int times = 16;
	for( int i = 0; i < times; i++ )
    		SEND_LOG(clientSock, "WRITE PAGE 1");
        usleep(100000);
        ASSERT_EQ(times, log_monitor.write_count);
    }

    TEST_P(QTMonitorUnitTest, LogWritePageMutiplePageOneTime) {
    	SEND_LOG(clientSock, "WRITE PAGE 16");
        usleep(100000);
        ASSERT_EQ(16, log_monitor.write_count);
    }


    TEST_P(QTMonitorUnitTest, LogWriteSectorOnePage) {
	SEND_LOG(clientSock, "WRITE SECTOR 1");
        usleep(100000);
        ASSERT_EQ(1, log_monitor.write_sector_count);
    }

    TEST_P(QTMonitorUnitTest, CumulativeLogWriteSectorOnePageMutipleTime) {
	int times = 16;
	for( int i = 0; i < times; i++ )
		SEND_LOG(clientSock, "WRITE SECTOR 1");
        usleep(100000);
        ASSERT_EQ(times, log_monitor.write_sector_count);
    }

    TEST_P(QTMonitorUnitTest, LogWriteSectorMutiplePageOneTime) {
	SEND_LOG(clientSock, "WRITE SECTOR 16");
        usleep(100000);
        ASSERT_EQ(16, log_monitor.write_sector_count);
    }


    TEST_P(QTMonitorUnitTest, LogReadPageOnePage) {
    	SEND_LOG(clientSock, "READ PAGE 1");
        usleep(100000);
        ASSERT_EQ(1, log_monitor.read_count);
    }

    TEST_P(QTMonitorUnitTest, LogReadPageOnePageMutipleTime) {
    	for( int i = 0; i < 2; i++ )
    		SEND_LOG(clientSock, "READ PAGE 1");
        usleep(100000);
        ASSERT_EQ(2, log_monitor.read_count);
    }

    TEST_P(QTMonitorUnitTest, LogReadPageMutiplePageOneTime) {
    	SEND_LOG(clientSock, "READ PAGE 16");
        usleep(100000);
        ASSERT_EQ(16, log_monitor.read_count);
    }


    TEST_P(QTMonitorUnitTest, LogReadSectorOnePage) {
	SEND_LOG(clientSock, "READ SECTOR 1");
        usleep(100000);
        ASSERT_EQ(1, log_monitor.read_sector_count);
    }

    TEST_P(QTMonitorUnitTest, LogReadSectorOnePageMutipleTime) {
	for( int i = 0; i < 2; i++ )
		SEND_LOG(clientSock, "READ SECTOR 1");
        usleep(100000);
		ASSERT_EQ(2, log_monitor.read_sector_count);
    }

    TEST_P(QTMonitorUnitTest, LogReadSectorMutiplePageOneTime) {
	SEND_LOG(clientSock, "READ SECTOR 16");
        usleep(100000);
        ASSERT_EQ(16, log_monitor.read_sector_count);
    }


    TEST_P(QTMonitorUnitTest, LogWriteSpeed) {
    	SEND_LOG(clientSock, "WRITE BW 0.2233");
        usleep(100000);
        ASSERT_EQ(0.2233, log_monitor.write_speed);
    }

    TEST_P(QTMonitorUnitTest, LogWriteSpeedChange) {
    	SEND_LOG(clientSock, "WRITE BW 0.2233");
    	SEND_LOG(clientSock, "WRITE BW 0.5432");
        usleep(100000);
        ASSERT_EQ(0.5432, log_monitor.write_speed);
    }

    TEST_P(QTMonitorUnitTest, LogReadSpeed) {
     	SEND_LOG(clientSock, "READ BW 0.2233");
         usleep(100000);
         ASSERT_EQ(0.2233, log_monitor.read_speed);
     }

    TEST_P(QTMonitorUnitTest, LogReadSpeedChange) {
    	SEND_LOG(clientSock, "READ BW 0.2233");
    	SEND_LOG(clientSock, "READ BW 0.5432");
        usleep(100000);
        ASSERT_EQ(0.5432, log_monitor.read_speed);
    }

    TEST_P(QTMonitorUnitTest, LogGCCount) {
     	SEND_LOG(clientSock, "GC ");
     	SEND_LOG(clientSock, "GC");
     	SEND_LOG(clientSock, "GC");
         usleep(100000);
         ASSERT_EQ(3, log_monitor.gc_count);
     }

    TEST_P(QTMonitorUnitTest, LogWriteAmplificationSpeed) {
     	SEND_LOG(clientSock, "WB AMP 0.2233");
         usleep(100000);
         ASSERT_EQ(0.2233, log_monitor.write_amplification);
     }

    TEST_P(QTMonitorUnitTest, LogUtils) {
     	SEND_LOG(clientSock, "UTIL 0.2233");
         usleep(100000);
         ASSERT_EQ(0.2233, log_monitor.utils);
     }


} //namespace
