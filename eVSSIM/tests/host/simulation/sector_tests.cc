
#include "base_emulator_tests.h"

using namespace std;

namespace sector_tests {

    class SectorUnitTest : public BaseTest {
        public:
            virtual void SetUp() {
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
            for(size_t p=0; p < ssd_config->get_pages_ns(DEFAULT_NSID); p++){
                ASSERT_EQ(FTL_SUCCESS, _FTL_WRITE_SECT(g_device_index, DEFAULT_NSID, p * ssd_config->get_page_size(), 1, NULL));
            }
        }
    }

    TEST_P(SectorUnitTest, RandomOnePageAtTimeWrite) {
        SSDConf* ssd_config = base_test_get_ssd_config();

        for(int x=0; x<2 /*8*/; x++){
            for(size_t p=0; p < ssd_config->get_pages_ns(DEFAULT_NSID); p++){
                ASSERT_EQ(FTL_SUCCESS, _FTL_WRITE_SECT(g_device_index, DEFAULT_NSID, (rand() % ssd_config->get_pages_ns(DEFAULT_NSID)) * ssd_config->get_page_size(), 1, NULL));
            }
        }
    }

    TEST_P(SectorUnitTest, MixSequentialAndRandomOnePageAtTimeWrite) {
        SSDConf* ssd_config = base_test_get_ssd_config();

        for(int x=0; x<2; x++){
            for(size_t p=0; p < ssd_config->get_pages_ns(DEFAULT_NSID); p++){
                ASSERT_EQ(FTL_SUCCESS, _FTL_WRITE_SECT(g_device_index, DEFAULT_NSID, (rand() % ssd_config->get_pages_ns(DEFAULT_NSID)) * ssd_config->get_page_size(), 1, NULL));
            }
            for(size_t p=0; p < ssd_config->get_pages_ns(DEFAULT_NSID); p++){
                ASSERT_EQ(FTL_SUCCESS, _FTL_WRITE_SECT(g_device_index, DEFAULT_NSID, p * ssd_config->get_page_size(), 1, NULL));
            }
        }
    }

    TEST_P(SectorUnitTest, SequentialOnePageAtTimeWriteInSeveralNS) {
        SSDConf* ssd_config = base_test_get_ssd_config();

        for(int x=0; x<2 /*8*/; x++){
            for(size_t p=0; (p < ssd_config->get_pages_ns(DEFAULT_NSID)) && (p < ssd_config->get_pages_ns(OTHER_NSID)); p++){
                ASSERT_EQ(FTL_SUCCESS, _FTL_WRITE_SECT(g_device_index, DEFAULT_NSID, p * ssd_config->get_page_size(), 1, NULL));
                ASSERT_EQ(FTL_SUCCESS, _FTL_WRITE_SECT(g_device_index, OTHER_NSID, p * ssd_config->get_page_size(), 1, NULL));
            }
        }
    }

} //namespace
