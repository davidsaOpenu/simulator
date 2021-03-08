
#include "base_emulator_tests.h"

extern bool g_nightly_mode;

using namespace std;

namespace {

    class SectorUnitTest : public BaseEmulatorTests {
        public:
            virtual void SetUp() {
                BaseEmulatorParams test_params = GetParam();
                BaseEmulatorTests::SetUp(test_params);
                INIT_LOG_MANAGER();
            }

            virtual void TearDown() {
                BaseEmulatorTests::TearDown();
                TERM_LOG_MANAGER();
            }
    }; // OccupySpaceStressTest

    std::vector<BaseEmulatorParams> GetTestParams() {
        std::vector<BaseEmulatorParams> test_params;

        if (g_nightly_mode) {
            printf("Running in Nightly mode\n");

            for (unsigned int i = 0; i < sizeof(parameters::Allsizemb)/sizeof(parameters::Allsizemb[0]); i++) {
                for (unsigned int j = 0; j < sizeof(parameters::Allflashnb)/sizeof(parameters::Allflashnb[0]); j++) {
                    test_params.push_back(BaseEmulatorParams(parameters::Allsizemb[i], parameters::Allflashnb[j]));
                }
            }
        } else {
            for (unsigned int i = 0; i < sizeof(parameters::Allsizemb)/sizeof(parameters::Allsizemb[0]); i++) {
                test_params.push_back(BaseEmulatorParams(parameters::Allsizemb[i], DEFAULT_FLASH_NB));
            }
        }

        return test_params;
    }

    INSTANTIATE_TEST_CASE_P(DiskSize, SectorUnitTest, ::testing::ValuesIn(GetTestParams()));

    TEST_P(SectorUnitTest, SequentialOnePageAtTimeWrite) {
        for(int x=0; x<2 /*8*/; x++){
            for(size_t p=0; p < pages_; p++){
                ASSERT_EQ(FTL_SUCCESS, _FTL_WRITE_SECT(p * CONST_PAGE_SIZE_IN_BYTES, 1));
            }
        }
    }

    TEST_P(SectorUnitTest, RandomOnePageAtTimeWrite) {
        for(int x=0; x<2 /*8*/; x++){
            for(size_t p=0; p < pages_; p++){
                ASSERT_EQ(FTL_SUCCESS, _FTL_WRITE_SECT((rand() % pages_) * CONST_PAGE_SIZE_IN_BYTES, 1));
            }
        }
    }

    TEST_P(SectorUnitTest, MixSequentialAndRandomOnePageAtTimeWrite) {
        for(int x=0; x<2; x++){
            for(size_t p=0; p < pages_; p++){
                ASSERT_EQ(FTL_SUCCESS, _FTL_WRITE_SECT((rand() % pages_) * CONST_PAGE_SIZE_IN_BYTES, 1));
            }
            for(size_t p=0; p < pages_; p++){
                ASSERT_EQ(FTL_SUCCESS, _FTL_WRITE_SECT(p * CONST_PAGE_SIZE_IN_BYTES, 1));
            }
        }
    }
} //namespace
