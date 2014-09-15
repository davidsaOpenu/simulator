
extern "C" {
#include "ssd.h"
#include "ftl.h"
#include "ftl_obj_strategy.h"
#include "ftl_sect_strategy.h"
#include "uthash.h"
}
extern "C" int g_init;
extern "C" int g_server_create;
extern "C" int g_init_log_server;

#define GTEST_DONT_DEFINE_FAIL 1
#include <gtest/gtest.h>

#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <math.h>

using namespace std;

namespace {

    class OccupySpaceStressTest : public ::testing::TestWithParam<size_t> {
        public:
            virtual void SetUp() {
                size_t mb = 512;//GetParam();
                pages_= mb * (1048576 / 4096); // number_of_pages = disk_size (in MB) * 1048576 / page_size
                size_t block_x_flash = pages_ / 8; // all_blocks_on_all_flashes = number_of_pages / pages_in_block
                size_t flash = block_x_flash / 4096; // number_of_flashes = all_blocks_on_all_flashes / number_of_blocks_in_flash
                ofstream ssd_conf("data/ssd.conf", ios_base::out | ios_base::trunc);
                ssd_conf << "FILE_NAME ./data/ssd.img\n"
                    "PAGE_SIZE 4096\n"
                    "PAGE_NB 10\n" // 8 pages per block +2 pages over-provision = 125% of disk size
                    "SECTOR_SIZE 1\n"
                    "FLASH_NB " << flash << "\n" // see calculations above
                    "BLOCK_NB 4096\n"
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
                    "STAT_PATH /tmp/stat.csv\n";
                ssd_conf.close();
                SSD_INIT();
                INIT_OBJ_STRATEGY();
                object_size_ = GetParam();
                int object_pages = (int)ceil(1.0 * object_size_ / PAGE_SIZE);
                objects_in_ssd_ = (int)(PAGES_IN_SSD / object_pages);
            }
            virtual void TearDown() {
                SSD_TERM();
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
            int object_size_;
            int objects_in_ssd_;
    }; // OccupySpaceStressTest

//    INSTANTIATE_TEST_CASE_P(DiskSize, OccupySpaceStressTest, ::testing::Values(512 /*MB*/ ,1024 /*1G*/, 4096 /*4GB*/)); //Values are in MB
    INSTANTIATE_TEST_CASE_P(DiskSize, OccupySpaceStressTest, ::testing::Values(2        /* 1/2 page */
                                                                              ,4        /* 3/2 page */
//                                                                              ,2 * 1024 /* 2 MB */
//                                                                              ,6 * 1024 /* 6 MB */
                                                                              )); //Values are in KB

    
    TEST_P(OccupySpaceStressTest, SimpleObjectCreate) {
        printf("SimpleObjectCreate test started\n");
        printf("Page no.:%ld\nPage size:%d\n",PAGES_IN_SSD,PAGE_SIZE);
        printf("Object size: %d MB\n",object_size_);
        // Fill the disk with objects
        for(size_t p=0; p < objects_in_ssd_; p++){
            //printf("%ld/%ld\n",(long)p,PAGES_IN_SSD);
            ASSERT_LT(0, _FTL_OBJ_CREATE(object_size_));
        }
        // At this step there shouldn't be any free page
        ASSERT_EQ(FAIL, _FTL_OBJ_CREATE(object_size_));      
        printf("SimpleObjectCreate test ended\n");
    }

    TEST_P(OccupySpaceStressTest, SimpleObjectCreateWrite) {
        printf("SimpleObjectCreateWrite test started\n");
        printf("Page no.:%ld\nPage size:%d\n",PAGES_IN_SSD,PAGE_SIZE);
        printf("Object size: %d MB\n",object_size_);

        // used to keep all the assigned ids
        int objects[objects_in_ssd_];

        // Fill 50% of the disk with objects
        for(int p=0; p < objects_in_ssd_ / 2; p++){
            int new_obj = _FTL_OBJ_CREATE(object_size_);
            ASSERT_LT(0, new_obj);
            objects[p] = new_obj;
        }
        // Write PAGE_SIZE data to each one
        for(size_t p=0; p < objects_in_ssd_/2; p++){
            ASSERT_EQ(SUCCESS, _FTL_OBJ_WRITE(objects[p],0,PAGE_SIZE));
        }
        printf("SimpleObjectCreateWrite test ended\n");
    }

    TEST_P(OccupySpaceStressTest, SimpleObjectCreateRead) {
        printf("SimpleObjectCreateRead test started\n");
        printf("Page no.:%ld\nPage size:%d\n",PAGES_IN_SSD,PAGE_SIZE);
        printf("Object size: %d MB\n",object_size_);

        // used to keep all the assigned ids
        int objects[objects_in_ssd_];

        // Fill 50% of the disk with objects
        for(int p=0; p < objects_in_ssd_/2; p++){
            int new_obj = _FTL_OBJ_CREATE(object_size_);
            ASSERT_LT(0, new_obj);
            objects[p] = new_obj;
        }
        // Read PAGE_SIZE data from each one
        for(size_t p=0; p < objects_in_ssd_/2; p++){
            ASSERT_EQ(SUCCESS, _FTL_OBJ_READ(objects[p],0,PAGE_SIZE));
        }
        printf("SimpleObjectCreateRead test ended\n");
    }
    
    TEST_P(OccupySpaceStressTest, SimpleObjectCreateDelete) {
        printf("SimpleObjectCreateDelete test started\n");
        printf("Page no.:%ld\nPage size:%d\n",PAGES_IN_SSD,PAGE_SIZE);
        printf("Object size: %d MB\n",object_size_);
        
        // used to keep all the assigned ids
        int objects[objects_in_ssd_];
        
        // Fill the disk with objects
        for(int p=0; p < objects_in_ssd_; p++){
            int new_obj = _FTL_OBJ_CREATE(object_size_);
            ASSERT_LT(0, new_obj);
            objects[p] = new_obj;
        }
        
        // Now make sure we can't create a new object, aka the disk is full
        ASSERT_EQ(FAIL, _FTL_OBJ_CREATE(object_size_)); 
        
        // Delete all objects
        for (int p=0; p < objects_in_ssd_; p++) {
            ASSERT_EQ(SUCCESS, _FTL_OBJ_DELETE(objects[p]));
        }
        
        // And try to fill the disk again with the same number of sized objects
        for(int p=0; p < objects_in_ssd_; p++){
            ASSERT_LT(0, _FTL_OBJ_CREATE(object_size_));
        }
        
        printf("SimpleObjectCreateDelete test ended\n");
    }

    // TAKES TOO MUCH TIME! PLUS THERE'S NO NEED TO REPEAT IT 4 TIMES
    /*
    TEST_P(OccupySpaceStressTest, ObjectGrowthTest) {
        printf("ObjectGrowth test started\n");
        printf("Page no.:%ld\nPage size:%d\n",PAGES_IN_SSD,PAGE_SIZE);
        
        // create a 1-page object
        int obj_id = _FTL_OBJ_CREATE(object_size_);
        ASSERT_LT(0, obj_id);

        // continuously extend it
        for(int p=1; p < PAGES_IN_SSD; p++) {
            ASSERT_EQ(SUCCESS, _FTL_OBJ_WRITE(obj_id,(p-1)*PAGE_SIZE,PAGE_SIZE));
        }

        // we should've covered the whole disk by now, so another write should fail
        ASSERT_EQ(FAIL, _FTL_OBJ_WRITE(obj_id,PAGES_IN_SSD*PAGE_SIZE,PAGE_SIZE)); 
        
        printf("ObjectGrowth test ended\n");
    }*/




    // ORIGINAL SECTOR-BASED TESTS
    /*
    TEST_P(OccupySpaceStressTest, SequentialOnePageAtTimeWrite) {
        printf("SequentialOnePageAtTimeWrite test started\n");
        
        for(int x=0; x<8; x++){
            printf("Test x=%d started\n", x);
            for(size_t p=0; p < pages_; p++){
                ASSERT_EQ(SUCCESS, _FTL_WRITE(p * 4096, 0, 1));
            }
            printf("Test x=%d ended\n", x);
        }
        
        printf("SequentialOnePageAtTimeWrite test ended\n");
    }

    TEST_P(OccupySpaceStressTest, RandomOnePageAtTimeWrite) {
        printf("RandomOnePageAtTimeWrite test started\n");
        
        for(int x=0; x<8; x++){
            printf("Test x=%d started\n", x);
            for(size_t p=0; p < pages_; p++){
                ASSERT_EQ(SUCCESS, _FTL_WRITE((rand() % pages_) * 4096, 0, 1));
            }
            printf("Test x=%d ended\n", x);
        }
        
        printf("RandomOnePageAtTimeWrite test ended\n");
    }

    TEST_P(OccupySpaceStressTest, MixSequentialAndRandomOnePageAtTimeWrite) {
        printf("MixSequentialAndRandomOnePageAtTimeWrite test started\n");
        
        for(int x=0; x<2; x++){
            printf("Test x=%d started\n", x);
            for(size_t p=0; p < pages_; p++){
                ASSERT_EQ(SUCCESS, _FTL_WRITE((rand() % pages_) * 4096, 0, 1));
            }
            for(size_t p=0; p < pages_; p++){
                ASSERT_EQ(SUCCESS, _FTL_WRITE(p * 4096, 0, 1));
            }
            printf("Test x=%d ended\n", x);
        }
        
        printf("MixSequentialAndRandomOnePageAtTimeWrite test ended\n");
    }
    */
} //namespace
