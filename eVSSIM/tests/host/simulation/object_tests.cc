
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

#include "base_emulator_tests.h"

#include <math.h>
#include <assert.h>
#include <typeinfo>


using namespace std;


namespace object_tests {
    class ObjectUnitTest : public BaseTest {
        public:
            virtual void SetUp() {
                BaseTest::SetUp();
                INIT_OBJ_STRATEGY();
                INIT_LOG_MANAGER(g_device_index);

                SSDConf* ssd_config = base_test_get_ssd_config();

                object_size_ = ssd_config->get_object_size();
                int object_pages = (int)ceil(1.0 * object_size_ / GET_PAGE_SIZE(g_device_index)); // ceil because we can't have a page belong to 2 objects
                objects_in_ssd_ = (unsigned int)((devices[g_device_index].pages_in_ssd - devices[g_device_index].page_nb) / object_pages); //over-provisioning of exactly one block
            }
            virtual void TearDown() {
                BaseTest::TearDown();
                TERM_LOG_MANAGER(g_device_index);
            }

        protected:
            int object_size_;
            unsigned int objects_in_ssd_;
    }; // OccupySpaceStressTest

    std::vector<SSDConf*> GetTestParams() {
        std::vector<SSDConf*> ssd_configs;

        size_t page_size = 4096;
        size_t sector_size = 1;
        size_t page_nb = 10;
        size_t block_nb = 128;

        for (unsigned int i = 0; i < BASE_TEST_ARRAY_SIZE(parameters::Allobjsize); i++) {
                SSDConf* config = new SSDConf(
                        page_size, page_nb, sector_size, DEFAULT_FLASH_NB, block_nb, DEFAULT_FLASH_NB);
                config->set_object_size(parameters::Allobjsize[i]);
                ssd_configs.push_back(config);
        }

        return ssd_configs;
    }

    INSTANTIATE_TEST_CASE_P(DiskSize, ObjectUnitTest, ::testing::ValuesIn(GetTestParams()));

    TEST_P(ObjectUnitTest, SimpleObjectCreate) {
        printf("SimpleObjectCreate test started\n");
        printf("Page no.:%ld\nPage size:%d\n", devices[g_device_index].pages_in_ssd, GET_PAGE_SIZE(g_device_index));
        printf("Object size: %d bytes\n", object_size_);
        obj_id_t object_locator = { .object_id = 0, .partition_id = USEROBJECT_PID_LB };

        // Fill the disk with objects
        for (unsigned long p = 0; p < objects_in_ssd_; p++) {
            object_locator.object_id = USEROBJECT_OID_LB + p;
            bool res = _FTL_OBJ_CREATE(g_device_index, object_locator, object_size_);
            ASSERT_TRUE(res);
        }

        // At this step there shouldn't be any free page
        //ASSERT_EQ(FAIL, _FTL_OBJ_CREATE(object_size_));
        printf("SimpleObjectCreate test ended\n");
    }


    TEST_P(ObjectUnitTest, SimpleObjectCreateWrite) {
        printf("SimpleObjectCreateWrite test started\n");
        printf("Page no.:%ld\nPage size:%d\n", devices[g_device_index].pages_in_ssd, GET_PAGE_SIZE(g_device_index));
        printf("Object size: %d bytes\n", object_size_);

        // used to keep all the assigned ids
        obj_id_t objects[objects_in_ssd_];

        // Fill 50% of the disk with objects
        for (unsigned long p = 1; p < objects_in_ssd_ / 2; p++) {
            objects[p].object_id = USEROBJECT_OID_LB + p;
            objects[p].partition_id = USEROBJECT_PID_LB;

            bool res = _FTL_OBJ_CREATE(g_device_index, objects[p], object_size_);
            ASSERT_TRUE(res);
        }

        char *wrbuf = (char *)Calloc(1, GET_PAGE_SIZE(g_device_index));

        // Write GET_PAGE_SIZE(g_device_index) data to each one
        for (unsigned long p = 1; p < objects_in_ssd_ / 2; p++) {
            ASSERT_EQ(FTL_SUCCESS, _FTL_OBJ_WRITE(g_device_index, objects[p], wrbuf, 0, GET_PAGE_SIZE(g_device_index)));
        }

        free(wrbuf);

        printf("SimpleObjectCreateWrite test ended\n");
    }

    TEST_P(ObjectUnitTest, SimpleObjectCreateWriteRead) {
        printf("SimpleObjectCreateWriteRead test started\n");
        printf("Page no.:%ld\nPage size:%d\n", devices[g_device_index].pages_in_ssd, GET_PAGE_SIZE(g_device_index));
        printf("Object size: %d bytes\n", object_size_);

        // used to keep all the assigned ids
        obj_id_t objects[objects_in_ssd_];
        memset(objects, 0x0, objects_in_ssd_ * sizeof(obj_id_t));

        char *wrbuf = (char *)Calloc(1, object_size_);

        // Fill 50% of the disk with objects
        for(unsigned long p= 1; p < objects_in_ssd_ / 2; p++) {
            objects[p].object_id = USEROBJECT_OID_LB + p;
            objects[p].partition_id = USEROBJECT_PID_LB;
            bool res = _FTL_OBJ_CREATE(g_device_index, objects[p], object_size_);
            ASSERT_TRUE(res);

            memset(wrbuf, 0x0, object_size_);
            sprintf(wrbuf,"%lu", objects[p].object_id);
            res = _FTL_OBJ_WRITE(g_device_index, objects[p], wrbuf, 0, object_size_);

            ASSERT_TRUE(res);
        }

        // length and read buffer
        uint32_t len = 0;
        char *rdbuf = (char *)Calloc(1, GET_PAGE_SIZE(g_device_index) / 2);

        // Read GET_PAGE_SIZE(g_device_index) / 2 data from each one
        for(unsigned long p = 1; p < objects_in_ssd_ / 2; p++) {
            len = GET_PAGE_SIZE(g_device_index) / 2;
            memset(rdbuf, 0x0, GET_PAGE_SIZE(g_device_index) / 2);
            ASSERT_EQ(FTL_SUCCESS, _FTL_OBJ_READ(g_device_index, objects[p], rdbuf, 0, &len));
            sprintf(wrbuf, "%lu", objects[p].object_id);
            ASSERT_EQ(0, strcmp(rdbuf, wrbuf));
        }

        free(rdbuf);
        free(wrbuf);

        printf("SimpleObjectCreateWriteRead test ended\n");
    }

    TEST_P(ObjectUnitTest, SimpleObjectCreateWriteDelete) {
        printf("SimpleObjectCreateDelete test started\n");
        printf("Page no.:%ld\nPage size:%d\n",devices[g_device_index].pages_in_ssd,GET_PAGE_SIZE(g_device_index));
        printf("Object size: %d bytes\n",object_size_);

        // used to keep all the assigned ids
        obj_id_t objects[objects_in_ssd_];
        memset(objects, 0x0, objects_in_ssd_ * sizeof(obj_id_t));

        char *wrbuf = (char *)Calloc(1, object_size_);
        memset(wrbuf, 0xff, object_size_);

        // Fill the disk with objects
        for(unsigned long p = 1; p < objects_in_ssd_ / 2; p++) {
            objects[p].object_id = USEROBJECT_OID_LB + p;
            objects[p].partition_id = USEROBJECT_PID_LB;

            bool res = _FTL_OBJ_CREATE(g_device_index, objects[p], object_size_);
            ASSERT_TRUE(res);
        }

        // Now make sure we can't create a new object, aka the disk is full
        // ASSERT_EQ(FTL_FAILURE, _FTL_OBJ_CREATE(object, object_size_));

        // Delete all objects
        for (unsigned long p = 1; p < objects_in_ssd_ / 2; p++) {
            // TODO: _FTL_OBJ_DELETE wont really free the pages
            ASSERT_EQ(FTL_SUCCESS, _FTL_OBJ_DELETE(g_device_index, objects[p]));
        }

        // And try to fill the disk again with the same number of sized objects
        for(unsigned long p = 1; p < objects_in_ssd_ / 2; p++) {
            bool res = _FTL_OBJ_CREATE(g_device_index, objects[p], object_size_);
            ASSERT_TRUE(res);
        }

        free(wrbuf);

        printf("SimpleObjectCreateDelete test ended\n");
    }

    // This UT uses the offset. let's comment it for now.
    /*
    TEST_P(ObjectUnitTest, ObjectGrowthTest) {
        unsigned int final_object_size = objects_in_ssd_ * object_size_;
        printf("ObjectGrowth test started\n");
        printf("Page no.:%ld\nPage size:%d\n",devices[g_device_index].pages_in_ssd,GET_PAGE_SIZE(g_device_index));
        printf("Initial object size: %d bytes\n",object_size_);
        printf("Final object size: %d bytes\n",final_object_size);

        obj_id_t tempObj = { .object_id = USEROBJECT_OID_LB, .partition_id = USEROBJECT_PID_LB };
        // create an object_size_bytes_ - sized object
        bool res = _FTL_OBJ_CREATE(tempObj, object_size_);
        ASSERT_TRUE(res);

        char *wrbuf = (char *)Calloc(1, object_size_);
        memset(wrbuf, 0x0, object_size_);

        unsigned int size = object_size_;
        // continuously extend it with object_size_bytes_ chunks
        while (size < final_object_size) {
            ASSERT_EQ(FTL_SUCCESS, _FTL_OBJ_WRITE(tempObj, wrbuf, size, object_size_));
            size += object_size_;
        }

        // we should've covered the whole disk by now, so another write should fail
        //ASSERT_EQ(FAIL, _FTL_OBJ_WRITE(obj_id, size, object_size_));
        free(wrbuf);

        printf("ObjectGrowth test ended\n");
    }
    */

} //namespace
