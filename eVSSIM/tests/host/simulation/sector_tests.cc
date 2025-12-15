
#include "base_emulator_tests.h"

#include <ctime>

using namespace std;

namespace sector_tests {

    class SectorUnitTest : public BaseTest {
        public:
            virtual void SetUp() {
                // Set the seed for the rand() with current time
                srand(time(NULL));

                BaseTest::SetUp();
                INIT_LOG_MANAGER(g_device_index);
            }

            virtual void TearDown() {
                BaseTest::TearDown(false);
                TERM_LOG_MANAGER(g_device_index);
                TERM_SSD_CONFIG();
            }
    };

    std::vector<SSDConf*> GetTestParams() {
        std::vector<SSDConf*> ssd_configs;

        for (unsigned int i = 0; i < BASE_TEST_ARRAY_SIZE(parameters::Allsizemb); i++) {
            ssd_configs.push_back(new SSDConf(parameters::Allsizemb[i]));
        }

        return ssd_configs;
    }

    INSTANTIATE_TEST_CASE_P(DiskSize, SectorUnitTest, ::testing::ValuesIn(GetTestParams()));

    TEST_P(SectorUnitTest, SequentialOnePageAtTimeWrite) {
        SSDConf* ssd_config = base_test_get_ssd_config();

        for(int x=0; x<2 /*8*/; x++){
            for(size_t p=0; p < ssd_config->get_pages_ns(ID_NS0); p++){
                ASSERT_EQ(FTL_SUCCESS, FTL_WRITE_SECT(g_device_index, ID_NS0, p * ssd_config->get_page_size(), 1, NULL));
            }
        }
    }

    TEST_P(SectorUnitTest, RandomOnePageAtTimeWrite) {
        SSDConf* ssd_config = base_test_get_ssd_config();

        for(int x=0; x<2 /*8*/; x++){
            for(size_t p=0; p < ssd_config->get_pages_ns(ID_NS0); p++){
                ASSERT_EQ(FTL_SUCCESS, FTL_WRITE_SECT(g_device_index, ID_NS0, (rand() % ssd_config->get_pages_ns(ID_NS0)) * ssd_config->get_page_size(), 1, NULL));
            }
        }
    }

    TEST_P(SectorUnitTest, MixSequentialAndRandomOnePageAtTimeWrite) {
        SSDConf* ssd_config = base_test_get_ssd_config();

        for(int x=0; x<2; x++){
            for(size_t p=0; p < ssd_config->get_pages_ns(ID_NS0); p++){
                ASSERT_EQ(FTL_SUCCESS, FTL_WRITE_SECT(g_device_index, ID_NS0, (rand() % ssd_config->get_pages_ns(ID_NS0)) * ssd_config->get_page_size(), 1, NULL));
            }
            for(size_t p=0; p < ssd_config->get_pages_ns(ID_NS0); p++){
                ASSERT_EQ(FTL_SUCCESS, FTL_WRITE_SECT(g_device_index, ID_NS0, p * ssd_config->get_page_size(), 1, NULL));
            }
        }
    }

    TEST_P(SectorUnitTest, WriteSameAddresses) {
        SSDConf* ssd_config = base_test_get_ssd_config();

        for(size_t i=0; i < ssd_config->get_pages_ns(ID_NS0); i++){
            ASSERT_EQ(FTL_SUCCESS, FTL_WRITE_SECT(g_device_index, ID_NS0, 10, 1, NULL));
        }

        ASSERT_EQ(FTL_SUCCESS, FTL_READ_SECT(g_device_index, ID_NS0, 10, 1, NULL));
    }

    TEST_P(SectorUnitTest, OnePageAtTimeReadBeforeWrite) {
        SSDConf* ssd_config = base_test_get_ssd_config();

        for(int x=0; x<2 /*8*/; x++){
            for(size_t p=0; (p < ssd_config->get_pages_ns(ID_NS0)) && (p < ssd_config->get_pages_ns(ID_NS1)); p++){
                ASSERT_EQ(FTL_FAILURE, FTL_READ_SECT(g_device_index, ID_NS0, p * ssd_config->get_page_size(), 1, NULL));
            }
        }
    }

    TEST_P(SectorUnitTest, OnePageAtTimeWriteAndRead) {
        SSDConf* ssd_config = base_test_get_ssd_config();

        for(int x=0; x<2 /*8*/; x++){
            for(size_t p=0; (p < ssd_config->get_pages_ns(ID_NS0)) && (p < ssd_config->get_pages_ns(ID_NS1)); p++){
                ASSERT_EQ(FTL_SUCCESS, FTL_WRITE_SECT(g_device_index, ID_NS0, p * ssd_config->get_page_size(), 1, NULL));
                ASSERT_EQ(FTL_SUCCESS, FTL_READ_SECT(g_device_index, ID_NS0, p * ssd_config->get_page_size(), 1, NULL));
            }
        }
    }

    TEST_P(SectorUnitTest, OnePageAtTimeWriteAndTryToReadInDeferentNS) {
        SSDConf* ssd_config = base_test_get_ssd_config();

        for(int x=0; x<2 /*8*/; x++){
            for(size_t p=0; (p < ssd_config->get_pages_ns(ID_NS0)) && (p < ssd_config->get_pages_ns(ID_NS1)); p++){
                ASSERT_EQ(FTL_SUCCESS, FTL_WRITE_SECT(g_device_index, ID_NS0, p * ssd_config->get_page_size(), 1, NULL));
                ASSERT_EQ(FTL_FAILURE, FTL_READ_SECT(g_device_index, ID_NS1, p * ssd_config->get_page_size(), 1, NULL));
            }
        }
    }

    TEST_P(SectorUnitTest, SequentialOnePageAtTimeWriteInSeveralNS) {
        SSDConf* ssd_config = base_test_get_ssd_config();

        for(int x=0; x<2 /*8*/; x++){
            for(size_t p=0; (p < ssd_config->get_pages_ns(ID_NS0)) && (p < ssd_config->get_pages_ns(ID_NS1)); p++){
                ASSERT_EQ(FTL_SUCCESS, FTL_WRITE_SECT(g_device_index, ID_NS0, p * ssd_config->get_page_size(), 1, NULL));
                ASSERT_EQ(FTL_SUCCESS, FTL_WRITE_SECT(g_device_index, ID_NS1, p * ssd_config->get_page_size(), 1, NULL));
            }
        }
    }

    TEST_P(SectorUnitTest, WriteBeyondNamespaceBoundary) {
        SSDConf* ssd_config = base_test_get_ssd_config();

        ASSERT_EQ(FTL_SUCCESS, FTL_WRITE_SECT(g_device_index, ID_NS0, (ssd_config->get_pages_ns(ID_NS0) - 1) * ssd_config->get_page_size(), 1, NULL));
        ASSERT_EQ(FTL_FAILURE, FTL_WRITE_SECT(g_device_index, ID_NS0, ssd_config->get_pages_ns(ID_NS0) * ssd_config->get_page_size(), 1, NULL));

        ASSERT_EQ(FTL_SUCCESS, FTL_WRITE_SECT(g_device_index, ID_NS1, (ssd_config->get_pages_ns(ID_NS1)  - 1) * ssd_config->get_page_size(), 1, NULL));
        ASSERT_EQ(FTL_FAILURE, FTL_WRITE_SECT(g_device_index, ID_NS1, ssd_config->get_pages_ns(ID_NS1) * ssd_config->get_page_size(), 1, NULL));
    }

    TEST_P(SectorUnitTest, ReadBeyondNamespaceBoundary) {
        SSDConf* ssd_config = base_test_get_ssd_config();

        ASSERT_EQ(FTL_SUCCESS, FTL_WRITE_SECT(g_device_index, ID_NS0, (ssd_config->get_pages_ns(ID_NS0) - 1) * ssd_config->get_page_size(), 1, NULL));
        ASSERT_EQ(FTL_SUCCESS, FTL_WRITE_SECT(g_device_index, ID_NS1, (ssd_config->get_pages_ns(ID_NS1)  - 1) * ssd_config->get_page_size(), 1, NULL));

        ASSERT_EQ(FTL_SUCCESS, FTL_READ_SECT(g_device_index, ID_NS0, (ssd_config->get_pages_ns(ID_NS0) - 1) * ssd_config->get_page_size(), 1, NULL));
        ASSERT_EQ(FTL_FAILURE, FTL_READ_SECT(g_device_index, ID_NS0, ssd_config->get_pages_ns(ID_NS0) * ssd_config->get_page_size(), 1, NULL));

        ASSERT_EQ(FTL_SUCCESS, FTL_READ_SECT(g_device_index, ID_NS1, (ssd_config->get_pages_ns(ID_NS1)  - 1) * ssd_config->get_page_size(), 1, NULL));
        ASSERT_EQ(FTL_FAILURE, FTL_READ_SECT(g_device_index, ID_NS1, ssd_config->get_pages_ns(ID_NS1) * ssd_config->get_page_size(), 1, NULL));
    }

    TEST_P(SectorUnitTest, WriteNSToFull) {
        SSDConf* ssd_config = base_test_get_ssd_config();

        ASSERT_EQ(FTL_FAILURE, FTL_WRITE_SECT(g_device_index, ID_NS0, ssd_config->get_pages_ns(ID_NS1) * ssd_config->get_page_size(), 1, NULL));
        ASSERT_EQ(FTL_FAILURE, FTL_WRITE_SECT(g_device_index, ID_NS1, ssd_config->get_pages_ns(ID_NS1) * ssd_config->get_page_size(), 1, NULL));

        for(int x=0; x<2 /*8*/; x++){
            for(size_t p=0; (p < ssd_config->get_pages_ns(ID_NS0)); p++){
                ASSERT_EQ(FTL_SUCCESS, FTL_WRITE_SECT(g_device_index, ID_NS0, p * ssd_config->get_page_size(), 1, NULL));
            }
            for(size_t p=0; (p < ssd_config->get_pages_ns(ID_NS1)); p++){
                ASSERT_EQ(FTL_SUCCESS, FTL_WRITE_SECT(g_device_index, ID_NS1, p * ssd_config->get_page_size(), 1, NULL));
            }
            for(size_t p=0; (p < ssd_config->get_pages_ns(ID_NS0)); p++){
                ASSERT_EQ(FTL_SUCCESS, FTL_READ_SECT(g_device_index, ID_NS0, p * ssd_config->get_page_size(), 1, NULL));
            }
            for(size_t p=0; (p < ssd_config->get_pages_ns(ID_NS1)); p++){
                ASSERT_EQ(FTL_SUCCESS, FTL_READ_SECT(g_device_index, ID_NS1, p * ssd_config->get_page_size(), 1, NULL));
            }
        }

        ASSERT_EQ(FTL_FAILURE, FTL_WRITE_SECT(g_device_index, ID_NS0, ssd_config->get_pages_ns(ID_NS1) * ssd_config->get_page_size(), 1, NULL));
        ASSERT_EQ(FTL_FAILURE, FTL_WRITE_SECT(g_device_index, ID_NS1, ssd_config->get_pages_ns(ID_NS1) * ssd_config->get_page_size(), 1, NULL));
    }

    TEST_P(SectorUnitTest, ReadWriteInvalidSectorAddressTest)
    {
        SSDConf* ssd_config = base_test_get_ssd_config();

        // Invalid address for the default namespace.
        ASSERT_EQ(FTL_FAILURE, FTL_WRITE_SECT(g_device_index, ID_NS0, ssd_config->get_pages_ns(ID_NS0) * ssd_config->get_page_size(), 1, NULL));
        ASSERT_EQ(FTL_FAILURE, FTL_READ_SECT(g_device_index, ID_NS0, ssd_config->get_pages_ns(ID_NS0) * ssd_config->get_page_size(), 1, NULL));

        // Invalid address for the other namespace.
        ASSERT_EQ(FTL_FAILURE, FTL_WRITE_SECT(g_device_index, ID_NS1, ssd_config->get_pages_ns(ID_NS1) * ssd_config->get_page_size(), 1, NULL));
        ASSERT_EQ(FTL_FAILURE, FTL_READ_SECT(g_device_index, ID_NS1, ssd_config->get_pages_ns(ID_NS1) * ssd_config->get_page_size(), 1, NULL));

        // Useing invalid namespace ID.
        ASSERT_EQ(FTL_FAILURE, FTL_WRITE_SECT(g_device_index, 4, 0, 1, NULL));
        ASSERT_EQ(FTL_FAILURE, FTL_READ_SECT(g_device_index, 4, 0, 1, NULL));
    }

    TEST_P(SectorUnitTest, WriteAndReadRandomPageAtTime)
    {
        SSDConf* ssd_config = base_test_get_ssd_config();

        uint8_t write_buffer[ssd_config->get_page_size()];
        uint8_t read_buffer[ssd_config->get_page_size()];

        uint32_t random_nsid = 0;
        uint64_t random_page = 0;

        // Init the testing file.
        ASSERT_EQ(_FTL_CREATE(g_device_index), FTL_SUCCESS);
        ASSERT_EQ(ONFI_INIT(g_device_index), ONFI_SUCCESS);


        for(int x=0; x < 2; x++){
            for(uint64_t p=0; p < ssd_config->get_device_pages_ns(); p++){
                // Get a random namespace index.
                random_nsid = rand() % 2;
                random_page = rand() % ssd_config->get_pages_ns(random_nsid);

                // Fill buffer with random data.
                for (uint64_t i = 0; i < ssd_config->get_page_size(); ++i) {
                    write_buffer[i] = (uint8_t)(rand() % UINT8_MAX);
                }

                ASSERT_EQ(FTL_SUCCESS, FTL_WRITE_SECT(g_device_index, random_nsid, random_page * ssd_config->get_page_size(), ssd_config->get_page_size(), write_buffer));
                ASSERT_EQ(FTL_SUCCESS, FTL_READ_SECT(g_device_index, random_nsid, random_page * ssd_config->get_page_size(), ssd_config->get_page_size(), read_buffer));

                ASSERT_EQ(memcmp(write_buffer, read_buffer, ssd_config->get_page_size()), 0);
            }
        }

        // Clean the data file.
        remove(GET_FILE_NAME(g_device_index));
    }

} //namespace
