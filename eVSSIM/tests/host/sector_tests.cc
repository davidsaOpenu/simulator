
#include "base_emulator_tests.h"

using namespace std;

extern string g_tests_filter;

namespace sector_tests {

    class SectorUnitTest : public BaseTest {
        public:
            virtual void SetUp() {
                BaseTest::SetUp();
                INIT_LOG_MANAGER();
            }

            virtual void TearDown() {
                BaseTest::TearDown();
                TERM_LOG_MANAGER();
            }
    }; // OccupySpaceStressTest

    std::vector<SSDConf*> GetTestParams() {
        std::vector<SSDConf*> ssd_configs;
        string sector_unitests_class_name = "SectorUnitTest";

        std::cout << "g_tests_filter == " << g_tests_filter << "\n";
        std::cout << "sector_unitests_class_name == " << sector_unitests_class_name << "\n";

        if (g_tests_filter.compare(sector_unitests_class_name) == 0 ||
                g_tests_filter.compare("*") == 0) {
            for (unsigned int i = 0; i < BASE_TEST_ARRAY_SIZE(parameters::Allsizemb); i++) {
                ssd_configs.push_back(new SSDConf(parameters::Allsizemb[i]));
            }
        }

        return ssd_configs;
    }

    INSTANTIATE_TEST_CASE_P(DiskSize, SectorUnitTest, ::testing::ValuesIn(GetTestParams()));

    TEST_P(SectorUnitTest, SequentialOnePageAtTimeWrite) {
        SSDConf* ssd_config = GetParam();

        for(int x=0; x<2 /*8*/; x++){
            for(size_t p=0; p < ssd_config->get_pages(); p++){
                ASSERT_EQ(FTL_SUCCESS, _FTL_WRITE_SECT(p * ssd_config->get_page_size(), 1));
            }
        }
    }

} //namespace
