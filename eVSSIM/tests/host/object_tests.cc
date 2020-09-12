
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
bool g_nightly_mode = false;

#define GTEST_DONT_DEFINE_FAIL 1

#include <gtest/gtest.h>

#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <math.h>
#include <assert.h>

// default value for flash number
#define DEFAULT_FLASH_NB 4

/**
 * paramters that the tests run with
 * @param objsize - object size parameter
 *  testing object sizes of 2048 and 4098 megabytes
 * @param flashnb - number of flash memories
 *  testing flash numbers 2,4,8,16 and 32
 */
namespace parameters
{
    enum objsize
    {
        os1 = 2048,
        os2 = 4098
    };

    static const objsize Allobjsize[] = { os1, os2 };

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
    // check if CI_MODE environment variable is set to NIGHTLY
    const char* ci_mode = getenv("CI_MODE");
    if(ci_mode != NULL && std::string(ci_mode) == "NIGHTLY")
        g_nightly_mode = true;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--nightly") == 0) {
            g_nightly_mode = true;
        }
    }
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

using namespace std;

namespace {
    class ObjectUnitTest : public ::testing::TestWithParam<std::pair<size_t,size_t> > {
        public:
            virtual void SetUp() {
                std::pair<size_t,size_t> params = GetParam();
                size_t flash_nb = params.second;
                ofstream ssd_conf("data/ssd.conf", ios_base::out | ios_base::trunc);
                ssd_conf << "FILE_NAME ./data/ssd.img\n"
                    "PAGE_SIZE 4096\n"
                    "PAGE_NB 10\n"
                    "SECTOR_SIZE 1\n"
                    "FLASH_NB " << flash_nb << "\n"
                    "BLOCK_NB 128\n"
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
                    "STORAGE_STRATEGY 2\n"; // object strategy
                ssd_conf.close();
                FTL_INIT();
                INIT_OBJ_STRATEGY();
                INIT_LOG_MANAGER();
                object_size_ = params.first;
                int object_pages = (int)ceil(1.0 * object_size_ / GET_PAGE_SIZE()); // ceil because we can't have a page belong to 2 objects
                objects_in_ssd_ = (unsigned int)((PAGES_IN_SSD - BLOCK_NB)/ object_pages); //over-provisioning of exactly one block
#ifndef NO_OSD
                osd_init();
#endif
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
#ifndef NO_OSD
                osd_term();
#endif
            }
            virtual void osd_init() {
                const char *root = "/tmp/osd/";
                int ret_sys_val = system("rm -rf /tmp/osd");
                (void) ret_sys_val; // Not Used Variable
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

    std::vector<std::pair<size_t,size_t> > GetParams() {
        std::vector<std::pair<size_t,size_t> > list;

        if (g_nightly_mode) {
            printf("Running in Nightly mode\n");

            for( unsigned int i = 0;
                    i < sizeof(parameters::Allobjsize)/sizeof(parameters::Allobjsize[0]); i++ )
            {
                for( unsigned int j = 0;
                        j < sizeof(parameters::Allflashnb)/sizeof(parameters::Allflashnb[0]); j++ )
                    // first paramter in the pair is the object size
                    // second paramter in the pair is number of flash memories in ssd
                    list.push_back(std::make_pair(parameters::Allobjsize[i], parameters::Allflashnb[j] ));
            }

            return list;
        }

        const int constFlashNum = DEFAULT_FLASH_NB;
        for( unsigned int i = 0;
                i < sizeof(parameters::Allobjsize)/sizeof(parameters::Allobjsize[0]); i++ )
            list.push_back(std::make_pair(parameters::Allobjsize[i], constFlashNum ));

            return list;

    }

    INSTANTIATE_TEST_CASE_P(DiskSize, ObjectUnitTest, ::testing::ValuesIn(GetParams()));
    TEST_P(ObjectUnitTest, SimpleObjectCreate) {
        printf("SimpleObjectCreate test started\n");
        printf("Page no.:%ld\nPage size:%d\n",PAGES_IN_SSD,GET_PAGE_SIZE());
        printf("Object size: %d bytes\n",object_size_);
        obj_id_t object_locator = { .object_id = 0, .partition_id = 0 };
#ifndef NO_OSD
        char *wrbuf = (char *)Calloc(1, object_size_);
#endif

        // Fill the disk with objects
        for(unsigned long p=1; p < objects_in_ssd_; p++){
            object_locator.object_id = p;
            bool res = _FTL_OBJ_CREATE(object_locator, object_size_);
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
        obj_id_t object_locator = { .object_id = 0, .partition_id = 0 };

        // used to keep all the assigned ids
        obj_id_t objects[objects_in_ssd_];

#ifndef NO_OSD
        char *wrbuf = (char *)Calloc(1, object_size_);
#endif

        // Fill 50% of the disk with objects
        for(unsigned long p=1; p < objects_in_ssd_ / 2; p++){
            object_locator.object_id = p;
            bool res = _FTL_OBJ_CREATE(object_locator, object_size_);
            ASSERT_TRUE(res);
            objects[p].object_id = p;
            objects[p].partition_id = 0 ;
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
            ASSERT_EQ(FTL_SUCCESS, _FTL_OBJ_WRITE(objects[p],(buf_ptr_t) wrbuf,0,GET_PAGE_SIZE()));
#ifndef NO_OSD
            ASSERT_EQ(0, osd_write(&osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB + objects[p].object_id,
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
        obj_id_t objects[objects_in_ssd_];

#ifndef NO_OSD
        char *wrbuf = (char *)Calloc(1, object_size_);
#endif
        // Fill 50% of the disk with objects
        for(unsigned long p=1; p < objects_in_ssd_/2; p++){
            objects[p].object_id = p;
            objects[p].partition_id = 0;
            bool res = _FTL_OBJ_CREATE(objects[p], object_size_);
            ASSERT_TRUE(res);

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
            ASSERT_EQ(FTL_SUCCESS, _FTL_OBJ_READ(objects[p], 0, GET_PAGE_SIZE()));
#ifndef NO_OSD
            // read and compare with the expected unique data
            ASSERT_EQ(0, osd_read(&osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB + objects[p].object_id,
                        GET_PAGE_SIZE()/2, 0, NULL, (uint8_t *)rdbuf, &len, 0, osd_sense, DDT_CONTIG));
            sprintf(wrbuf, "%lu", objects[p].object_id);
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
        obj_id_t objects[objects_in_ssd_];

#ifndef NO_OSD
        char *wrbuf = (char *)Calloc(1, object_size_);
#endif

        // Fill the disk with objects
        for(unsigned long p=1; p < objects_in_ssd_; p++){
            objects[p].object_id = p;
            objects[p].partition_id = 0;
            bool res = _FTL_OBJ_CREATE(objects[p], object_size_);
            ASSERT_TRUE(res);

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
            ASSERT_EQ(0, osd_remove(&osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB + objects[p].object_id, cdb_cont_len, osd_sense));
#endif
        }

        // And try to fill the disk again with the same number of sized objects
        for(unsigned long p=1; p < objects_in_ssd_; p++){
            bool res = _FTL_OBJ_CREATE(objects[p], object_size_);
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

        obj_id_t tempObj = { .object_id = 1000, .partition_id = 0 };
        // create an object_size_bytes_ - sized object
        bool res = _FTL_OBJ_CREATE(tempObj, object_size_);
        ASSERT_TRUE(res);

#ifndef NO_OSD
        char *wrbuf = (char *)Calloc(1, object_size_);
        ASSERT_EQ(0, osd_create_and_write(&osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB + tempObj.object_id, object_size_, 0,
                    (uint8_t *)wrbuf, cdb_cont_len, 0, osd_sense, DDT_CONTIG));
#endif

        unsigned int size = object_size_;
        // continuously extend it with object_size_bytes_ chunks
        while (size < final_object_size) {
            ASSERT_EQ(FTL_SUCCESS, _FTL_OBJ_WRITE(tempObj,(buf_ptr_t)wrbuf, size, object_size_));
            size += object_size_;
#ifndef NO_OSD
            ASSERT_EQ(0, osd_write(&osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB + tempObj.object_id,
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
