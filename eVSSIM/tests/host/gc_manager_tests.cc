
#include "base_emulator_tests.h"

int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}


using namespace std;

int gc_collection_mock_call_counter = 0;
int gc_new_page_mock_call_counter = 0;
ftl_ret_val GARBAGE_COLLECTION_MOCK(int mapping_index, int l2, bool isObjectStrategy) {
    gc_collection_mock_call_counter++;
    return GARBAGE_COLLECTION_DEFAULT(mapping_index, l2, isObjectStrategy);
}

ftl_ret_val GET_NEW_PAGE_MOCK(int mode, int mapping_index, uint32_t* ppn) {
    gc_new_page_mock_call_counter++;
    return GET_NEW_PAGE_DEFAULT(mode, mapping_index, ppn);
}

namespace {

    class GCManagerUnitTest : public BaseEmulatorTests {}; // OccupySpaceStressTest

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

    INSTANTIATE_TEST_CASE_P(DiskSize, GCManagerUnitTest, ::testing::ValuesIn(GetParams()));
    TEST_P(GCManagerUnitTest, GCAlgorithmSet) {

        gc_collection_mock_call_counter = 0;
        gc_new_page_mock_call_counter = 0;

        GC_SET_COLLETION_ALGO(&GARBAGE_COLLECTION_MOCK);
        for(int x=0; x<2 /*8*/; x++){
            for(size_t p=0; p < pages_; p++){
                _FTL_WRITE_SECT((rand() % pages_) * CONST_PAGE_SIZE_IN_BYTES, 1);
            }
        }
        ASSERT_LE(0, gc_collection_mock_call_counter);
        ASSERT_LE(0, gc_new_page_mock_call_counter);
    }

} //namespace
