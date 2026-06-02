
extern "C" {
    #include "onfi.h"
};

#include <gtest/gtest.h>

using namespace std;

namespace onfi_ops_test
{
    class OnfiOpsTest : public ::testing::Test {
    };

    TEST_F(OnfiOpsTest, StatusRegisterSizeTest) {
        ASSERT_EQ(sizeof(onfi_status_reg_t), 1u);
    }

    TEST_F(OnfiOpsTest, ParamterPageStructSizeTest) {
        ASSERT_EQ(sizeof(onfi_param_page_t), 256u);
    }

    TEST_F(OnfiOpsTest, OnfiUpdateStatusRegisterWorksCorrectly) {
        onfi_status_reg_t status;
        memset(&status, 0, sizeof(onfi_status_reg_t));

        _ONFI_UPDATE_STATUS_REGISTER(&status, ONFI_FAILURE);
        ASSERT_EQ(status.FAIL, 1u);
        ASSERT_EQ(status.FAILC, 0u);

        _ONFI_UPDATE_STATUS_REGISTER(&status, ONFI_FAILURE);
        ASSERT_EQ(status.FAIL, 1u);
        ASSERT_EQ(status.FAILC, 1u);

        _ONFI_UPDATE_STATUS_REGISTER(&status, ONFI_SUCCESS);
        ASSERT_EQ(status.FAIL, 0u);
        ASSERT_EQ(status.FAILC, 1u);

        _ONFI_UPDATE_STATUS_REGISTER(&status, ONFI_SUCCESS);
        ASSERT_EQ(status.FAIL, 0u);
        ASSERT_EQ(status.FAILC, 0u);
    }
};
