
#include "base_emulator_tests.h"

extern bool g_nightly_mode;

using namespace std;

namespace {

    class SectorUnitTest : public BaseEmulatorTests {}; // OccupySpaceStressTest

    std::vector<std::pair<size_t,size_t> > GetParams() {
        std::vector<std::pair<size_t,size_t> > list;

        if (g_nightly_mode) {
            printf("Running in Nightly mode\n");

            for( unsigned int i = 0;
                    i < sizeof(parameters::Allsizemb)/sizeof(parameters::Allsizemb[0]); i++ )
            {
                for( unsigned int j = 0;
                        j < sizeof(parameters::Allflashnb)/sizeof(parameters::Allflashnb[0]); j++ )
                    // first paramter in the pair is size of disk in mb
                    // second paramter in the pair is number of flash memories in ssd
                    list.push_back(std::make_pair(parameters::Allsizemb[i], parameters::Allflashnb[j] ));
            }

            return list;
        }

        const int constFlashNum = DEFAULT_FLASH_NB;
        for( unsigned int i = 0;
                i < sizeof(parameters::Allsizemb)/sizeof(parameters::Allsizemb[0]); i++ )
            list.push_back(std::make_pair(parameters::Allsizemb[i], constFlashNum ));

        return list;
    }

    INSTANTIATE_TEST_CASE_P(DiskSize, SectorUnitTest, ::testing::ValuesIn(GetParams()));
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
