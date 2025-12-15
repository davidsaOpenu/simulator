
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
                g_device_index = OBJECT_DEV;

                BaseTest::SetUp();
                INIT_LOG_MANAGER(g_device_index);

                SSDConf* ssd_config = base_test_get_ssd_config();

                object_size = ssd_config->get_object_size();

                // over-provisioning of exactly one block
                objects_in_default_ns = (unsigned int)(((ssd_config->get_pages_ns(ID_NS0) - ssd_config->get_page_nb())  * ssd_config->get_page_size()) / object_size);
            }

            virtual void TearDown() {
                BaseTest::TearDown(false);
                TERM_LOG_MANAGER(g_device_index);
                TERM_SSD_CONFIG();

                g_device_index = SECTOR_DEV;
            }

        protected:
            int object_size;
            unsigned int objects_in_default_ns;

    }; // OccupySpaceStressTest

    std::vector<SSDConf*> GetTestParams() {
        std::vector<SSDConf*> ssd_configs;

        const size_t page_size = 4096;
        const size_t sector_size = 1;
        const size_t page_nb = 10;
        const size_t block_nb = 128;
        const size_t default_ns_block_nb = (block_nb * DEFAULT_FLASH_NB) / 2;
        const size_t othere_ns_block_nb = (block_nb * DEFAULT_FLASH_NB) / 2;

        for (unsigned int i = 0; i < BASE_TEST_ARRAY_SIZE(parameters::Allobjsize); i++) {
                SSDConf* config = new SSDConf(
                        page_size, page_nb, sector_size, DEFAULT_FLASH_NB, block_nb, DEFAULT_FLASH_NB, default_ns_block_nb, othere_ns_block_nb);
                config->set_object_size(parameters::Allobjsize[i]);

                ssd_configs.push_back(config);
        }

        return ssd_configs;
    }

    INSTANTIATE_TEST_CASE_P(DiskSize, ObjectUnitTest, ::testing::ValuesIn(GetTestParams()));

    TEST_P(ObjectUnitTest, SimpleObjectCreate) {
        printf("Page no.:%ld\nPage size:%d\n", devices[g_device_index].pages_in_ssd, GET_PAGE_SIZE(g_device_index));
        printf("Object size: %d bytes\n", object_size);

        obj_id_t object_locator = { .object_id = 0, .partition_id = USEROBJECT_PID_LB };

        // Fill the disk with objects
        for (unsigned long p = 0; p < this->objects_in_default_ns; p++) {
            object_locator.object_id = USEROBJECT_OID_LB + p;

            ASSERT_TRUE(FTL_OBJ_CREATE(g_device_index, ID_NS0, object_locator, object_size));
        }

        // At this step there shouldn't be any free page.
        ASSERT_FALSE(FTL_OBJ_CREATE(g_device_index, ID_NS0, object_locator, object_size));

        // The other namespace isn't full.
        ASSERT_TRUE(FTL_OBJ_CREATE(g_device_index, ID_NS1, object_locator, object_size));
    }

    TEST_P(ObjectUnitTest, SimpleObjectCreateWrite) {
        // used to keep all the assigned id.
        obj_id_t object_locator = { .object_id = USEROBJECT_OID_LB, .partition_id = USEROBJECT_PID_LB };

        char *wrbuf = (char *)Calloc(1, GET_PAGE_SIZE(g_device_index));

        // Try to write to the object before it created
        ASSERT_EQ(FTL_FAILURE, FTL_OBJ_WRITE(g_device_index, ID_NS0, object_locator, wrbuf, 0, GET_PAGE_SIZE(g_device_index)));

        // Create a new object and write to it.
        ASSERT_TRUE(FTL_OBJ_CREATE(g_device_index, ID_NS0, object_locator, object_size));
        ASSERT_EQ(FTL_SUCCESS, FTL_OBJ_WRITE(g_device_index, ID_NS0, object_locator, wrbuf, 0, GET_PAGE_SIZE(g_device_index)));

        // Try to write into an object in different namespace
        ASSERT_EQ(FTL_FAILURE, FTL_OBJ_WRITE(g_device_index, ID_NS1, object_locator, wrbuf, 0, GET_PAGE_SIZE(g_device_index)));

        free(wrbuf);
    }

    TEST_P(ObjectUnitTest, ObjectCreateWrite) {
        printf("Page no.:%ld\nPage size:%d\n", devices[g_device_index].pages_in_ssd, GET_PAGE_SIZE(g_device_index));
        printf("Object size: %d bytes\n", object_size);

        // used to keep all the assigned id.
        obj_id_t object_locator = { .object_id = 0, .partition_id = USEROBJECT_PID_LB };

        // Fill 50% of the disk with objects
        for (unsigned long p = 1; p < this->objects_in_default_ns / 2; p++) {
            object_locator.object_id = USEROBJECT_OID_LB + p;

            ASSERT_TRUE(FTL_OBJ_CREATE(g_device_index, ID_NS0, object_locator, object_size));
        }

        char *wrbuf = (char *)Calloc(1, GET_PAGE_SIZE(g_device_index));

        // Write GET_PAGE_SIZE(g_device_index) data to each one
        for (unsigned long p = 1; p < this->objects_in_default_ns / 2; p++) {
            object_locator.object_id = USEROBJECT_OID_LB + p;

            ASSERT_EQ(FTL_SUCCESS, FTL_OBJ_WRITE(g_device_index, ID_NS0, object_locator, wrbuf, 0, GET_PAGE_SIZE(g_device_index)));
        }

        free(wrbuf);
    }

    TEST_P(ObjectUnitTest, SimpleObjectCreateWriteRead) {
        printf("Page no.:%ld\nPage size:%d\n", devices[g_device_index].pages_in_ssd, GET_PAGE_SIZE(g_device_index));
        printf("Object size: %d bytes\n", object_size);

        // used to keep all the assigned id
        obj_id_t object_locator = { .object_id = 0, .partition_id = USEROBJECT_PID_LB };

        char *wrbuf = (char *)Calloc(1, object_size);
        memset(wrbuf, 0x0, object_size);

        // Fill 50% of the disk with objects
        for (unsigned long p = 1; p < this->objects_in_default_ns / 2; p++) {
            object_locator.object_id = USEROBJECT_OID_LB + p;

            ASSERT_TRUE(FTL_OBJ_CREATE(g_device_index, ID_NS0, object_locator, object_size));

            sprintf(wrbuf, "%lu", object_locator.object_id);

            ASSERT_EQ(FTL_SUCCESS, FTL_OBJ_WRITE(g_device_index, ID_NS0, object_locator, wrbuf, 0, object_size));
        }

        // length and read buffer
        uint32_t len = GET_PAGE_SIZE(g_device_index) / 2;

        char *rdbuf = (char *)Calloc(1, len);
        memset(rdbuf, 0x0, len);

        // Read GET_PAGE_SIZE(g_device_index) / 2 data from each one
        for(unsigned long p = 1; p < this->objects_in_default_ns / 2; p++) {
            object_locator.object_id = USEROBJECT_OID_LB + p;

            len = GET_PAGE_SIZE(g_device_index) / 2;
            ASSERT_EQ(FTL_SUCCESS, FTL_OBJ_READ(g_device_index, ID_NS0, object_locator, rdbuf, 0, &len));

            sprintf(wrbuf, "%lu", object_locator.object_id);
            ASSERT_EQ(0, strcmp(rdbuf, wrbuf));
        }

        free(rdbuf);
        free(wrbuf);
    }

    TEST_P(ObjectUnitTest, SimpleObjectCreateWriteDelete) {
        printf("Page no.:%ld\nPage size:%d\n", devices[g_device_index].pages_in_ssd, GET_PAGE_SIZE(g_device_index));
        printf("Object size: %d bytes\n", object_size);

        // used to keep all the assigned id
        obj_id_t object_locator = { .object_id = 0, .partition_id = USEROBJECT_PID_LB };

        // Fill 50% of the disk with objects
        for (unsigned long p = 1; p < this->objects_in_default_ns / 2; p++) {
            object_locator.object_id = USEROBJECT_OID_LB + p;

            ASSERT_TRUE(FTL_OBJ_CREATE(g_device_index, ID_NS0, object_locator, object_size));
        }

        // Delete all objects
        for (unsigned long p = 1; p < this->objects_in_default_ns / 2; p++) {
            object_locator.object_id = USEROBJECT_OID_LB + p;

            ASSERT_FALSE(FTL_OBJ_CREATE(g_device_index, ID_NS0, object_locator, object_size));

            ASSERT_EQ(FTL_SUCCESS, FTL_OBJ_DELETE(g_device_index, ID_NS0, object_locator));

            ASSERT_TRUE(FTL_OBJ_CREATE(g_device_index, ID_NS0, object_locator, object_size));
        }
    }

    TEST_P(ObjectUnitTest, ObjectGrowthTest) {
        const unsigned int final_object_size = this->objects_in_default_ns * object_size;

        printf("Page no.:%ld\nPage size:%d\n",devices[g_device_index].pages_in_ssd,GET_PAGE_SIZE(g_device_index));
        printf("Initial object size: %d bytes\n",object_size);
        printf("Final object size: %d bytes\n",final_object_size);

        obj_id_t tempObj = { .object_id = USEROBJECT_OID_LB, .partition_id = USEROBJECT_PID_LB };

        // create an object_sizebytes_ - sized object
        ASSERT_TRUE(FTL_OBJ_CREATE(g_device_index, ID_NS0, tempObj, object_size));

        char *wrbuf = (char *)Calloc(1, object_size);
        memset(wrbuf, 0x0, object_size);

        unsigned int offset = 0;

        // continuously extend it with object_sizebytes_ chunks
        while (offset < final_object_size) {
            ASSERT_EQ(FTL_SUCCESS, FTL_OBJ_WRITE(g_device_index, ID_NS0, tempObj, wrbuf, 0, object_size));
            offset += object_size;
        }

        free(wrbuf);
    }

    TEST_P(ObjectUnitTest, ObjectInvalidNamespace) {

        // used to keep all the assigned id
        obj_id_t object_locator = { .object_id = USEROBJECT_OID_LB, .partition_id = USEROBJECT_PID_LB };

        // try to create an object in invalid namespace
        ASSERT_FALSE(FTL_OBJ_CREATE(g_device_index, ID_NS0 + 10, object_locator, object_size));

        // try to create a valid object.
        ASSERT_TRUE(FTL_OBJ_CREATE(g_device_index, ID_NS0, object_locator, object_size));

        char *buff = (char *)Calloc(1, object_size);

        uint32_t len = 0;
        ASSERT_EQ(FTL_FAILURE, FTL_OBJ_READ(g_device_index, ID_NS0 + 10, object_locator, buff, 0, &len));
        ASSERT_EQ(FTL_FAILURE, FTL_OBJ_WRITE(g_device_index, ID_NS0 + 10, object_locator, buff, 0, object_size));

        free(buff);
    }

} //namespace
