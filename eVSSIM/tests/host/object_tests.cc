
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

    INSTANTIATE_TEST_CASE_P(DiskSize, ObjectUnitTest, ::testing::Values(2048, // 1/2 page

                                                                        6144, // 1 1/2 pages
                                                                        2 * 1024 * 1024, // 2 MB
                                                                        6 * 1024 * 1024)); // 6 MB

    TEST_P(ObjectUnitTest, SimpleObjectCreate) {
        printf("SimpleObjectCreate test started\n");
        printf("Page no.:%ld\nPage size:%d\n",PAGES_IN_SSD,GET_PAGE_SIZE());
        printf("Object size: %d bytes\n",object_size_);
#ifndef NO_OSD
        char *wrbuf = (char *)Calloc(1, object_size_);
#endif
        
        // Fill the disk with objects
        for(unsigned long p=1; p < objects_in_ssd_; p++){
            bool res = _FTL_OBJ_CREATE(p, object_size_);
            ASSERT_TRUE(res);
#ifndef NO_OSD
            ASSERT_EQ(0, osd_create_and_write(&osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB + p, object_size_, 0,
                (uint8_t *)wrbuf, cdb_cont_len, 0, osd_sense, DDT_CONTIG));
#endif
        }
        
#ifndef NO_OSD
        free(wrbuf);
#endif
        
        // At this step there shouldn't be any free page
        //ASSERT_EQ(FAIL, _FTL_OBJ_CREATE(object_size_));      
        printf("SimpleObjectCreate test ended\n"); 
    }


    TEST_P(ObjectUnitTest, SimpleObjectCreateWrite) {
        printf("SimpleObjectCreateWrite test started\n");
        printf("Page no.:%ld\nPage size:%d\n",PAGES_IN_SSD,GET_PAGE_SIZE());
        printf("Object size: %d bytes\n",object_size_);

        // used to keep all the assigned ids
        int objects[objects_in_ssd_];
        
#ifndef NO_OSD
        char *wrbuf = (char *)Calloc(1, object_size_);
#endif
        
        // Fill 50% of the disk with objects
        for(unsigned long p=1; p < objects_in_ssd_ / 2; p++){
            bool res = _FTL_OBJ_CREATE(p, object_size_);
            ASSERT_TRUE(res);
            objects[p] = p;
#ifndef NO_OSD
            ASSERT_EQ(0, osd_create_and_write(&osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB + p, object_size_, 0,
                (uint8_t *)wrbuf, cdb_cont_len, 0, osd_sense, DDT_CONTIG));
#endif
        }
        
#ifndef NO_OSD
        free(wrbuf);
        wrbuf = (char *)Calloc(1, GET_PAGE_SIZE());
#endif
        
        // Write GET_PAGE_SIZE() data to each one
        for(unsigned long p=1; p < objects_in_ssd_/2; p++){
            ASSERT_EQ(FTL_SUCCESS, _FTL_OBJ_WRITE(objects[p],0,GET_PAGE_SIZE()));
#ifndef NO_OSD
            ASSERT_EQ(0, osd_write(&osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB + objects[p],
            		GET_PAGE_SIZE(), 0, (uint8_t *)wrbuf, 0, osd_sense, DDT_CONTIG));
#endif
        }
        
#ifndef NO_OSD
        free(wrbuf);
#endif
        
        printf("SimpleObjectCreateWrite test ended\n"); 
    }

    TEST_P(ObjectUnitTest, SimpleObjectCreateRead) {
        printf("SimpleObjectCreateRead test started\n");
        printf("Page no.:%ld\nPage size:%d\n",PAGES_IN_SSD,GET_PAGE_SIZE());
        printf("Object size: %d bytes\n",object_size_);

        // used to keep all the assigned ids
        int objects[objects_in_ssd_];
        
#ifndef NO_OSD
        char *wrbuf = (char *)Calloc(1, object_size_);
#endif
        // Fill 50% of the disk with objects
        for(unsigned long p=1; p < objects_in_ssd_/2; p++){
            bool res = _FTL_OBJ_CREATE(p, object_size_);
            ASSERT_TRUE(res);
            objects[p] = p;
#ifndef NO_OSD
            // insert unique data to the object
            sprintf(wrbuf,"%lu", p);
            ASSERT_EQ(0 ,osd_create_and_write(&osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB + p, object_size_, 0,
                (uint8_t *)wrbuf,cdb_cont_len, 0, osd_sense, DDT_CONTIG));
#endif
        }
        
#ifndef NO_OSD
        // length and read buffer
        uint64_t len;
        char *rdbuf = (char *)Calloc(1, GET_PAGE_SIZE()/2);
#endif
        
        // Read GET_PAGE_SIZE()/2 data from each one
        for(unsigned long p=1; p < objects_in_ssd_/2; p++){
            ASSERT_EQ(FTL_SUCCESS, _FTL_OBJ_READ(objects[p],0,GET_PAGE_SIZE()));
#ifndef NO_OSD
            // read and compare with the expected unique data
            ASSERT_EQ(0, osd_read(&osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB + objects[p],
            		GET_PAGE_SIZE()/2, 0, NULL, (uint8_t *)rdbuf, &len, 0, osd_sense, DDT_CONTIG));
            sprintf(wrbuf, "%u", objects[p]);
            ASSERT_EQ(0, strcmp(rdbuf, wrbuf));
#endif
        }
        
#ifndef NO_OSD
        free(rdbuf);
        free(wrbuf);
#endif
        
        printf("SimpleObjectCreateRead test ended\n");
    }

    TEST_P(ObjectUnitTest, SimpleObjectCreateDelete) {
        printf("SimpleObjectCreateDelete test started\n");
        printf("Page no.:%ld\nPage size:%d\n",PAGES_IN_SSD,GET_PAGE_SIZE());
        printf("Object size: %d bytes\n",object_size_);
        
        // used to keep all the assigned ids
        int objects[objects_in_ssd_];
        
#ifndef NO_OSD
        char *wrbuf = (char *)Calloc(1, object_size_);
#endif
        
        // Fill the disk with objects
        for(unsigned long p=1; p < objects_in_ssd_; p++){
            bool res = _FTL_OBJ_CREATE(p, object_size_);
            ASSERT_TRUE(res);
            objects[p] = p;
#ifndef NO_OSD
            ASSERT_EQ(0, osd_create_and_write(&osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB + p, object_size_, 0,
                (uint8_t *)wrbuf, cdb_cont_len, 0, osd_sense, DDT_CONTIG));
#endif
        }
        // Now make sure we can't create a new object, aka the disk is full
        // ASSERT_EQ(FAIL, _FTL_OBJ_CREATE(object_size_)); 

        // Delete all objects
        for (unsigned long p=1; p < objects_in_ssd_; p++) {
            ASSERT_EQ(FTL_SUCCESS, _FTL_OBJ_DELETE(objects[p]));
#ifndef NO_OSD
            ASSERT_EQ(0, osd_remove(&osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB + objects[p], cdb_cont_len, osd_sense));
#endif
        }

        // And try to fill the disk again with the same number of sized objects
        for(unsigned long p=1; p < objects_in_ssd_; p++){
            bool res = _FTL_OBJ_CREATE(p, object_size_);
            ASSERT_TRUE(res);
#ifndef NO_OSD
            ASSERT_EQ(0, osd_create_and_write(&osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB + p, object_size_, 0,
                (uint8_t *)wrbuf, cdb_cont_len, 0, osd_sense, DDT_CONTIG));
#endif
        }
        
#ifndef NO_OSD
        free(wrbuf);
#endif
        
        printf("SimpleObjectCreateDelete test ended\n");
    }

    TEST_P(ObjectUnitTest, ObjectGrowthTest) {
        unsigned int final_object_size = objects_in_ssd_ * object_size_;
        printf("ObjectGrowth test started\n");
        printf("Page no.:%ld\nPage size:%d\n",PAGES_IN_SSD,GET_PAGE_SIZE());
        printf("Initial object size: %d bytes\n",object_size_);
        printf("Final object size: %d bytes\n",final_object_size);

        int tempObj = 1000;
        // create an object_size_bytes_ - sized object
        bool res = _FTL_OBJ_CREATE(tempObj, object_size_);
        ASSERT_TRUE(res);

#ifndef NO_OSD
        char *wrbuf = (char *)Calloc(1, object_size_);
        ASSERT_EQ(0, osd_create_and_write(&osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB + tempObj, object_size_, 0,
            (uint8_t *)wrbuf, cdb_cont_len, 0, osd_sense, DDT_CONTIG));
#endif
        
        unsigned int size = object_size_;
        // continuously extend it with object_size_bytes_ chunks
        while (size < final_object_size) {
            ASSERT_EQ(FTL_SUCCESS, _FTL_OBJ_WRITE(tempObj, size, object_size_));
            size += object_size_;
#ifndef NO_OSD
            ASSERT_EQ(0, osd_write(&osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB + tempObj,
                object_size_, size, (uint8_t *)wrbuf, 0, osd_sense, DDT_CONTIG));
#endif
        }

        // we should've covered the whole disk by now, so another write should fail
        //ASSERT_EQ(FAIL, _FTL_OBJ_WRITE(obj_id, size, object_size_)); 
#ifndef NO_OSD
        free(wrbuf);
#endif
        
        printf("ObjectGrowth test ended\n");
    }

} //namespace


