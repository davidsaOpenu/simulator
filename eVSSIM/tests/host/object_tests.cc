
extern "C" {
#include "common.h"
#include "ftl_obj_strategy.h"
#include "uthash.h"

#include "osd.h"
#include "osd-util/osd-util.h"
#include "osd-util/osd-defs.h"
}
extern "C" int g_init;
extern "C" int clientSock;
extern "C" int g_init_log_server;

#define GTEST_DONT_DEFINE_FAIL 1

#include <gtest/gtest.h>

#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <math.h>
#include <assert.h>

using namespace std;

namespace {
    class ObjectUnitTest : public ::testing::TestWithParam<size_t> {
        public:
            virtual void SetUp() {
                ofstream ssd_conf("data/ssd.conf", ios_base::out | ios_base::trunc);
                ssd_conf << "FILE_NAME ./data/ssd.img\n"
                    "PAGE_SIZE 4096\n"
                    "PAGE_NB 10\n"
                    "SECTOR_SIZE 1\n"
                    "FLASH_NB 4\n"
                    "BLOCK_NB 128\n"
                    "PLANES_PER_FLASH 1\n"
                    "REG_WRITE_DELAY 82\n"
                    "CELL_PROGRAM_DELAY 900\n"
                    "REG_READ_DELAY 82\n"
                    "CELL_READ_DELAY 50\n"
                    "BLOCK_ERASE_DELAY 2000\n"
                    "CHANNEL_SWITCH_DELAY_R 16\n"
                    "CHANNEL_SWITCH_DELAY_W 33\n"
                    "CHANNEL_NB 1\n"
                    "STAT_TYPE 15\n"
                    "STAT_SCOPE 62\n"
                    "STAT_PATH /tmp/stat.csv\n"
                    "STORAGE_STRATEGY 2\n"; // object strategy
                ssd_conf.close();
            	FTL_INIT();
                INIT_OBJ_STRATEGY();
            #ifdef MONITOR_ON
            	INIT_LOG_MANAGER();
            #endif
                object_size_ = GetParam();
                int object_pages = (int)ceil(1.0 * object_size_ / GET_PAGE_SIZE()); // ceil because we can't have a page belong to 2 objects
                objects_in_ssd_ = (unsigned int)((PAGES_IN_SSD - BLOCK_NB)/ object_pages); //over-provisioning of exactly one block
#ifndef NO_OSD
                osd_init();
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
                clientSock = 0;
                g_init_log_server = 0;
#ifndef NO_OSD
                osd_term();
#endif
            }
            virtual void osd_init() {
                const char *root = "/tmp/osd/";
                system("rm -rf /tmp/osd");
                assert(!osd_open(root, &osd));
                osd_sense = (uint8_t*)Calloc(1, 1024);
                assert(!osd_create_partition(&osd, PARTITION_PID_LB, cdb_cont_len, osd_sense));
            }

            virtual void osd_term() {
                free(osd_sense);
                osd_close(&osd);
            }
        protected:
            int object_size_;
            unsigned int objects_in_ssd_;
            uint8_t *osd_sense;
            static const uint32_t cdb_cont_len = 0;
            struct osd_device osd;
    }; // OccupySpaceStressTest

    INSTANTIATE_TEST_CASE_P(DiskSize, ObjectUnitTest, ::testing::Values(2048,   // half page
                                                                        4098)); // one page
//                                                                        6144    // one and a half page
//                                                                        2 * 1024 * 1024, // 2 MB
//                                                                        6 * 1024 * 1024)); // 6 MB

    TEST_P(ObjectUnitTest, SimpleObjectCreate) {
#ifndef TMP_OBJ_DEV
    ASSERT_EQ(1,0);
#endif
    }


    TEST_P(ObjectUnitTest, SimpleObjectCreateWrite) {
#ifndef TMP_OBJ_DEV
    ASSERT_EQ(1,0);
#endif
    }

    TEST_P(ObjectUnitTest, SimpleObjectCreateRead) {
#ifndef TMP_OBJ_DEV
    ASSERT_EQ(1,0);
#endif
    }

    TEST_P(ObjectUnitTest, SimpleObjectCreateDelete) {
#ifndef TMP_OBJ_DEV
    ASSERT_EQ(1,0);
#endif
    }

    TEST_P(ObjectUnitTest, ObjectGrowthTest) {
#ifndef TMP_OBJ_DEV
    ASSERT_EQ(1,0);
#endif
    }

} //namespace


