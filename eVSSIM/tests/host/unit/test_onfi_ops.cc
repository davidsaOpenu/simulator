
extern "C" {
    #include "onfi.h"
};

#include <gtest/gtest.h>

using namespace std;

namespace onfi_ops_test
{
    class OnfiOpsTest : public ::testing::Test {
        virtual void SetUp()
        {
            ASSERT_EQ(_ONFI_INIT(), ONFI_SUCCESS);
        }
    };

    TEST_F(OnfiOpsTest, StatusRegisterSizeTest) {
        ASSERT_EQ(sizeof(onfi_status_reg_t), 1);
    }

    TEST_F(OnfiOpsTest, ParamterPageStructSizeTest) {
        ASSERT_EQ(256, sizeof(onfi_param_page_t));
    }

    TEST_F(OnfiOpsTest, OnfiUpdateStatusRegisterWorksCorrectly) {
        onfi_status_reg_t status;
        memset(&status, 0, sizeof(onfi_status_reg_t));

        _ONFI_UPDATE_STATUS_REGISTER(&status, ONFI_FAILURE);
        ASSERT_EQ(status.FAIL, 1);
        ASSERT_EQ(status.FAILC, 0);

        _ONFI_UPDATE_STATUS_REGISTER(&status, ONFI_FAILURE);
        ASSERT_EQ(status.FAIL, 1);
        ASSERT_EQ(status.FAILC, 1);

        _ONFI_UPDATE_STATUS_REGISTER(&status, ONFI_SUCCESS);
        ASSERT_EQ(status.FAIL, 0);
        ASSERT_EQ(status.FAILC, 1);

        _ONFI_UPDATE_STATUS_REGISTER(&status, ONFI_SUCCESS);
        ASSERT_EQ(status.FAIL, 0);
        ASSERT_EQ(status.FAILC, 0);
    }
};
