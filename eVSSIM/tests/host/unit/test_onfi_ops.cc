
extern "C" {
    #include "onfi.h"
};

#include <gtest/gtest.h>

using namespace std;

namespace onfi_ops_test
{
    class OnfiOpsTest : public ::testing::Test {};

    TEST_F(OnfiOpsTest, StatusRegisterSizeTest) {
        ASSERT_EQ(1, sizeof(onfi_status_reg_t));
    }

    TEST_F(OnfiOpsTest, ParamterPageStructSizeTest) {
        ASSERT_EQ(256, sizeof(onfi_param_page_t));
    }
};
