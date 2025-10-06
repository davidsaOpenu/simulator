
extern "C" {
    #include "onfi.h"
};

#include <gtest/gtest.h>

using namespace std;

namespace onfi_ops_test
{
    class OnfiOpsTest : public ::testing::Test {
    public:
        virtual void SetUp()
        {
            ASSERT_EQ(ONFI_INIT(DEVICE_INDEX), ONFI_SUCCESS);
        }

        template <size_t N>
        void ExpectAllZero(const uint8_t (&arr)[N], const char* name) {
            for (size_t i = 0; i < N; ++i) {
                EXPECT_EQ(arr[i], 0u) << name << " byte[" << i << "] not zero";
            }
        }

        static constexpr uint8_t DEVICE_INDEX = 0;
    };

    TEST_F(OnfiOpsTest, StatusRegisterSizeTest) {
        ASSERT_EQ(sizeof(onfi_status_reg_t), 1);
    }

    TEST_F(OnfiOpsTest, ParamterPageStructSizeTest) {
        ASSERT_EQ(sizeof(onfi_param_page_t), 256);
    }

    TEST_F(OnfiOpsTest, ParameterPageCorrectConstants) {
        onfi_param_page_t param_page;

        ASSERT_EQ(ONFI_READ_PARAMETER_PAGE(DEVICE_INDEX, 0, (uint8_t *)&param_page, sizeof(onfi_param_page_t)), ONFI_SUCCESS);
        ASSERT_EQ(memcmp(param_page.signature, "ONFI", 4), 0);
        ASSERT_GE(param_page.revision, 0x0001);

         // ===== validate reserved are 0 =====
        ExpectAllZero(param_page.reserved, "param_page.reserved");
        EXPECT_EQ(param_page.features.reserved, 0u) << "features.reserved must be 0";
        EXPECT_EQ(param_page.optional_commands.reserved, 0u) << "optional_commands.reserved must be 0";
        ExpectAllZero(param_page.manufacturer_info_block.reserved, "manufacturer_info_block.reserved");
        EXPECT_EQ(param_page.mem_org_block.partial_prog_attr.reserved1, 0u) << "partial_prog_attr.reserved1 must be 0";
        EXPECT_EQ(param_page.mem_org_block.partial_prog_attr.reserved2, 0u) << "partial_prog_attr.reserved2 must be 0";
        EXPECT_EQ(param_page.mem_org_block.interleave_addr_bits.reserved, 0u) << "interleave_addr_bits.reserved must be 0";
        EXPECT_EQ(param_page.mem_org_block.interleave_op_attr.reserved, 0u) << "interleave_op_attr.reserved must be 0";
        ExpectAllZero(param_page.mem_org_block.reserved, "mem_org_block.reserved");
    }

    TEST_F(OnfiOpsTest, ParameterPageCrcCorrect) {
        onfi_param_page_t param_page;

        ASSERT_EQ(ONFI_READ_PARAMETER_PAGE(DEVICE_INDEX, 0, (uint8_t *)&param_page, sizeof(onfi_param_page_t)), ONFI_SUCCESS);
        ASSERT_EQ(param_page.vendor_block.integrity_crc, _ONFI_CRC16((uint8_t *)&param_page, sizeof(param_page) - sizeof(param_page.vendor_block.integrity_crc)));
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
