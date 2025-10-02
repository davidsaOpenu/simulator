/*
 * Copyright 2025 The Open University of Israel
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <cmath>
#include "base_emulator_tests.h"

extern "C"
{
#include "onfi.h"
}

using namespace std;

namespace program_compatible_test
{

    class OnfiCommandsTest : public BaseTest
    {
    public:
        virtual void SetUp()
        {
            BaseTest::SetUp();
            INIT_LOG_MANAGER(g_device_index);
            pthread_mutex_lock(&g_lock); // prevent the GC thread from running
            ASSERT_EQ(_FTL_CREATE(g_device_index), FTL_SUCCESS);
            ASSERT_EQ(ONFI_INIT(g_device_index), ONFI_SUCCESS);
        }

        virtual void TearDown()
        {
            pthread_mutex_unlock(&g_lock);
            BaseTest::TearDown(false);
            TERM_LOG_MANAGER(g_device_index);
            remove(GET_FILE_NAME(g_device_index));
            TERM_SSD_CONFIG();
        }

        void AssertStatusRegisterOk()
        {
            onfi_status_reg_t status;
            ASSERT_EQ(ONFI_READ_STATUS(g_device_index, &status), ONFI_SUCCESS);
            ASSERT_EQ(status.FAIL, 0);
        }

        template <size_t N>
        void ExpectAllZero(const uint8_t (&arr)[N], const char* name) {
            for (size_t i = 0; i < N; ++i) {
                EXPECT_EQ(arr[i], 0u) << name << " byte[" << i << "] not zero";
            }
        }
    };

    std::vector<SSDConf *> GetTestParams()
    {
        std::vector<SSDConf *> ssd_configs;
        ssd_configs.push_back(new SSDConf(pow(2, 12), 10, 1, 8, pow(2, 12), 4));
        ssd_configs.push_back(new SSDConf(pow(2, 10), 8, 2, 8, pow(2, 12), 4));
        return ssd_configs;
    }

    INSTANTIATE_TEST_CASE_P(DiskSize, OnfiCommandsTest, ::testing::ValuesIn(GetTestParams()));

    /* ========== ONFI_READ_STATUS tests ========== */

    TEST_P(OnfiCommandsTest, InvalidDeviceIndexReadStatusFails)
    {
        onfi_status_reg_t status;
        ASSERT_EQ(ONFI_READ_STATUS(INVALID_DEVICE_INDEX, &status), ONFI_FAILURE);
    }

    TEST_P(OnfiCommandsTest, NullRegisterReadStatusFails)
    {
        ASSERT_EQ(ONFI_READ_STATUS(g_device_index, NULL), ONFI_FAILURE);
    }

    TEST_P(OnfiCommandsTest, StatusRegisterInitializedWithSuccess)
    {
        onfi_status_reg_t status;
        ASSERT_EQ(ONFI_READ_STATUS(g_device_index, &status), ONFI_SUCCESS);
        ASSERT_EQ(status.FAIL, 0);
        ASSERT_EQ(status.FAILC, 0);
        ASSERT_EQ(status.R, 0);
        ASSERT_EQ(status.R2, 0);
        ASSERT_EQ(status.R3, 0);
        ASSERT_EQ(status.ARDY, 1);
        ASSERT_EQ(status.RDY, 1);
        ASSERT_EQ(status.WP, 0);
    }

    TEST_P(OnfiCommandsTest, StatusRegisterFailValuesCorrectAfterPageProgramOperation)
    {
        onfi_status_reg_t status;
        size_t nprogrammed = 0;
        uint8_t data = 0xFF;

        ASSERT_EQ(ONFI_PAGE_PROGRAM(g_device_index, 0, 0, NULL, 0, NULL), ONFI_FAILURE);
        ASSERT_EQ(ONFI_READ_STATUS(g_device_index, &status), ONFI_SUCCESS);
        ASSERT_EQ(status.FAIL, 1);
        ASSERT_EQ(status.FAILC, 0);

        ASSERT_EQ(ONFI_PAGE_PROGRAM(g_device_index, 0, 0, NULL, 0, NULL), ONFI_FAILURE);
        ASSERT_EQ(ONFI_READ_STATUS(g_device_index, &status), ONFI_SUCCESS);
        ASSERT_EQ(status.FAIL, 1);
        ASSERT_EQ(status.FAILC, 1);

        ASSERT_EQ(ONFI_PAGE_PROGRAM(g_device_index, 0, 0, &data, sizeof(data), &nprogrammed), ONFI_SUCCESS);
        ASSERT_EQ(ONFI_READ_STATUS(g_device_index, &status), ONFI_SUCCESS);
        ASSERT_EQ(status.FAIL, 0);
        ASSERT_EQ(status.FAILC, 1);

        ASSERT_EQ(ONFI_PAGE_PROGRAM(g_device_index, 0, 0, &data, sizeof(data), &nprogrammed), ONFI_SUCCESS);
        ASSERT_EQ(ONFI_READ_STATUS(g_device_index, &status), ONFI_SUCCESS);
        ASSERT_EQ(status.FAIL, 0);
        ASSERT_EQ(status.FAILC, 0);
    }

    TEST_P(OnfiCommandsTest, StatusRegisterFailValuesCorrectAfterBlockEraseOperation)
    {
        SSDConf *ssd_config = base_test_get_ssd_config();
        onfi_status_reg_t status;

        ASSERT_EQ(ONFI_BLOCK_ERASE(g_device_index, ssd_config->get_pages()), ONFI_FAILURE);
        ASSERT_EQ(ONFI_READ_STATUS(g_device_index, &status), ONFI_SUCCESS);
        ASSERT_EQ(status.FAIL, 1);
        ASSERT_EQ(status.FAILC, 0);

        ASSERT_EQ(ONFI_BLOCK_ERASE(g_device_index, ssd_config->get_pages()), ONFI_FAILURE);
        ASSERT_EQ(ONFI_READ_STATUS(g_device_index, &status), ONFI_SUCCESS);
        ASSERT_EQ(status.FAIL, 1);
        ASSERT_EQ(status.FAILC, 1);

        ASSERT_EQ(ONFI_BLOCK_ERASE(g_device_index, 0), ONFI_SUCCESS);
        ASSERT_EQ(ONFI_READ_STATUS(g_device_index, &status), ONFI_SUCCESS);
        ASSERT_EQ(status.FAIL, 0);
        ASSERT_EQ(status.FAILC, 1);

        ASSERT_EQ(ONFI_BLOCK_ERASE(g_device_index, 0), ONFI_SUCCESS);
        ASSERT_EQ(ONFI_READ_STATUS(g_device_index, &status), ONFI_SUCCESS);
        ASSERT_EQ(status.FAIL, 0);
        ASSERT_EQ(status.FAILC, 0);
    }

    /* ========== ONFI_READ tests ========== */

    TEST_P(OnfiCommandsTest, InvalidDeviceIndexReadFails)
    {
        SSDConf *ssd_config = base_test_get_ssd_config();

        size_t nread = 0;
        unsigned char buffer[ssd_config->get_page_size()];
        ASSERT_EQ(ONFI_READ(INVALID_DEVICE_INDEX, 0, 0, buffer, ssd_config->get_page_size(), &nread), ONFI_FAILURE);
    }

    TEST_P(OnfiCommandsTest, NullBufferReadFails)
    {
        SSDConf *ssd_config = base_test_get_ssd_config();

        size_t nread = 0;
        ASSERT_EQ(ONFI_READ(g_device_index, 0, 0, NULL, ssd_config->get_page_size(), &nread), ONFI_FAILURE);
    }

    TEST_P(OnfiCommandsTest, NullReadAmountReadFails)
    {
        SSDConf *ssd_config = base_test_get_ssd_config();

        unsigned char buffer[ssd_config->get_page_size()];

        ASSERT_EQ(ONFI_READ(g_device_index, 0, 0, buffer, ssd_config->get_page_size(), NULL), ONFI_FAILURE);
    }

    TEST_P(OnfiCommandsTest, OutOfBoundsRowAddressReadFails)
    {
        SSDConf *ssd_config = base_test_get_ssd_config();

        unsigned char buffer[ssd_config->get_page_size()];
        size_t nread = 0;

        ASSERT_EQ(ONFI_READ(g_device_index, ssd_config->get_pages(), 0, buffer, ssd_config->get_page_size(), &nread), ONFI_FAILURE);
    }

    TEST_P(OnfiCommandsTest, OutOfBoundsColumnAddressReadFails)
    {
        SSDConf *ssd_config = base_test_get_ssd_config();

        unsigned char buffer[ssd_config->get_page_size()];
        size_t nread = 0;

        ASSERT_EQ(ONFI_READ(g_device_index, 0, ssd_config->get_page_size(), buffer, 1, &nread), ONFI_FAILURE);
    }

    TEST_P(OnfiCommandsTest, ReadAllSuccess)
    {
        SSDConf *ssd_config = base_test_get_ssd_config();

        size_t nread = 0;
        unsigned char buffer[ssd_config->get_page_size()];
        unsigned char reference_buffer[ssd_config->get_page_size()];
        memset(reference_buffer, 0xFF, ssd_config->get_page_size());

        for (size_t page = 0; page < ssd_config->get_pages(); ++page)
        {
            memset(buffer, 0x00, ssd_config->get_page_size());
            ASSERT_EQ(ONFI_READ(g_device_index, page, 0, buffer, ssd_config->get_page_size(), &nread), ONFI_SUCCESS);
            ASSERT_EQ(nread, ssd_config->get_page_size());
            ASSERT_EQ(memcmp(reference_buffer, buffer, ssd_config->get_page_size()), 0);
        }
    }

    TEST_P(OnfiCommandsTest, ReadPartialPageSuccess)
    {
        SSDConf *ssd_config = base_test_get_ssd_config();

        size_t nread = 0;
        unsigned char buffer[ssd_config->get_page_size()];
        unsigned char reference_buffer[ssd_config->get_page_size()];
        memset(reference_buffer, 0xFF, ssd_config->get_page_size());

        size_t column = ssd_config->get_page_size() / 4;
        size_t read_size = ssd_config->get_page_size() / 2;

        ASSERT_EQ(ONFI_READ(g_device_index, 0, column, buffer, read_size, &nread), ONFI_SUCCESS);
        ASSERT_EQ(nread, read_size);
        ASSERT_EQ(memcmp(reference_buffer, buffer, read_size), 0);
    }

    TEST_P(OnfiCommandsTest, ReadPartialPageWithPageOverflowSuccess)
    {
        SSDConf *ssd_config = base_test_get_ssd_config();

        size_t nread = 0;
        unsigned char buffer[ssd_config->get_page_size()];
        unsigned char reference_buffer[ssd_config->get_page_size()];
        memset(reference_buffer, 0xFF, ssd_config->get_page_size());

        size_t column = ssd_config->get_page_size() / 4;
        size_t expected_nread = ssd_config->get_page_size() - column;

        ASSERT_EQ(ONFI_READ(g_device_index, 0, column, buffer, ssd_config->get_page_size(), &nread), ONFI_SUCCESS);
        ASSERT_EQ(nread, expected_nread);
        ASSERT_EQ(memcmp(reference_buffer, buffer, nread), 0);
    }

    /* ========== ONFI_PAGE_PROGRAM tests ========== */

    TEST_P(OnfiCommandsTest, InvalidDeviceIndexPageProgramFails)
    {
        SSDConf *ssd_config = base_test_get_ssd_config();

        size_t nprogrammed = 0;
        unsigned char buffer[ssd_config->get_page_size()];
        ASSERT_EQ(ONFI_PAGE_PROGRAM(INVALID_DEVICE_INDEX, 0, 0, buffer, ssd_config->get_page_size(), &nprogrammed), ONFI_FAILURE);
    }

    TEST_P(OnfiCommandsTest, NullBufferPageProgramFails)
    {
        SSDConf *ssd_config = base_test_get_ssd_config();

        size_t nprogrammed = 0;
        ASSERT_EQ(ONFI_PAGE_PROGRAM(g_device_index, 0, 0, NULL, ssd_config->get_page_size(), &nprogrammed), ONFI_FAILURE);
    }

    TEST_P(OnfiCommandsTest, NullProgramAmountPageProgramFails)
    {
        SSDConf *ssd_config = base_test_get_ssd_config();

        unsigned char buffer[ssd_config->get_page_size()];

        ASSERT_EQ(ONFI_PAGE_PROGRAM(g_device_index, 0, 0, buffer, ssd_config->get_page_size(), NULL), ONFI_FAILURE);
    }

    TEST_P(OnfiCommandsTest, OutOfBoundsRowAddressPageProgramFails)
    {
        SSDConf *ssd_config = base_test_get_ssd_config();

        unsigned char buffer[ssd_config->get_page_size()];
        size_t nprogrammed = 0;

        ASSERT_EQ(ONFI_PAGE_PROGRAM(g_device_index, ssd_config->get_pages(), 0, buffer, ssd_config->get_page_size(), &nprogrammed), ONFI_FAILURE);
    }

    TEST_P(OnfiCommandsTest, OutOfBoundsColumnAddressPageProgramFails)
    {
        SSDConf *ssd_config = base_test_get_ssd_config();

        unsigned char buffer[ssd_config->get_page_size()];
        size_t nprogrammed = 0;

        ASSERT_EQ(ONFI_PAGE_PROGRAM(g_device_index, 0, ssd_config->get_page_size(), buffer, 1, &nprogrammed), ONFI_FAILURE);
    }

    TEST_P(OnfiCommandsTest, PageProgramAllSuccess)
    {
        SSDConf *ssd_config = base_test_get_ssd_config();

        size_t nprogrammed = 0;
        size_t nread = 0;
        unsigned char buffer[ssd_config->get_page_size()];
        unsigned char reference_buffer[ssd_config->get_page_size()];
        memset(reference_buffer, 0x00, ssd_config->get_page_size());

        for (size_t page = 0; page < ssd_config->get_pages(); ++page)
        {
            memset(buffer, 0x00, ssd_config->get_page_size());
            ASSERT_EQ(ONFI_PAGE_PROGRAM(g_device_index, page, 0, buffer, ssd_config->get_page_size(), &nprogrammed), ONFI_SUCCESS);
            ASSERT_EQ(nprogrammed, ssd_config->get_page_size());
            memset(buffer, 0xFF, ssd_config->get_page_size());
            ASSERT_EQ(ONFI_READ(g_device_index, page, 0, buffer, ssd_config->get_page_size(), &nread), ONFI_SUCCESS);
            ASSERT_EQ(memcmp(reference_buffer, buffer, ssd_config->get_page_size()), 0);
            AssertStatusRegisterOk();
        }
    }

    TEST_P(OnfiCommandsTest, PageProgramPartialPageSuccess)
    {
        SSDConf *ssd_config = base_test_get_ssd_config();

        size_t nprogrammed = 0;
        size_t nread = 0;
        unsigned char buffer[ssd_config->get_page_size()];
        unsigned char reference_buffer[ssd_config->get_page_size()];

        memset(buffer, 0x00, ssd_config->get_page_size());
        memset(reference_buffer, 0x00, ssd_config->get_page_size());

        size_t column = ssd_config->get_page_size() / 4;
        size_t program_size = ssd_config->get_page_size() / 2;

        ASSERT_EQ(ONFI_PAGE_PROGRAM(g_device_index, 0, column, buffer, program_size, &nprogrammed), ONFI_SUCCESS);
        ASSERT_EQ(nprogrammed, program_size);
        memset(buffer, 0xFF, ssd_config->get_page_size());
        ASSERT_EQ(ONFI_READ(g_device_index, 0, column, buffer, program_size, &nread), ONFI_SUCCESS);
        ASSERT_EQ(memcmp(reference_buffer, buffer, program_size), 0);
        AssertStatusRegisterOk();
    }

    TEST_P(OnfiCommandsTest, PageProgramPartialPageWithPageOverflowSuccess)
    {
        SSDConf *ssd_config = base_test_get_ssd_config();

        size_t nprogrammed = 0;
        size_t nread = 0;
        unsigned char buffer[ssd_config->get_page_size()];
        unsigned char reference_buffer[ssd_config->get_page_size()];

        memset(buffer, 0x00, ssd_config->get_page_size());
        memset(reference_buffer, 0x00, ssd_config->get_page_size());

        size_t column = ssd_config->get_page_size() / 4;
        size_t expected_nprogrammed = ssd_config->get_page_size() - column;

        ASSERT_EQ(ONFI_PAGE_PROGRAM(g_device_index, 0, column, buffer, ssd_config->get_page_size(), &nprogrammed), ONFI_SUCCESS);
        ASSERT_EQ(nprogrammed, expected_nprogrammed);
        memset(buffer, 0xFF, ssd_config->get_page_size());
        ASSERT_EQ(ONFI_READ(g_device_index, 0, column, buffer, ssd_config->get_page_size(), &nread), ONFI_SUCCESS);
        ASSERT_EQ(memcmp(reference_buffer, buffer, nprogrammed), 0);
        AssertStatusRegisterOk();
    }

    /* ========== ONFI_BLOCK_ERASE tests ========== */

    TEST_P(OnfiCommandsTest, InvalidDeviceIndexBlockEraseFails)
    {
        ASSERT_EQ(ONFI_BLOCK_ERASE(INVALID_DEVICE_INDEX, 0), ONFI_FAILURE);
    }

    TEST_P(OnfiCommandsTest, OutOfBoundsRowAddressBlockEraseFails)
    {
        SSDConf *ssd_config = base_test_get_ssd_config();

        ASSERT_EQ(ONFI_BLOCK_ERASE(g_device_index, ssd_config->get_pages()), ONFI_FAILURE);
    }

    TEST_P(OnfiCommandsTest, BlockEraseAllSuccess)
    {
        SSDConf *ssd_config = base_test_get_ssd_config();

        size_t nprogrammed = 0;
        size_t nread = 0;
        unsigned char buffer[ssd_config->get_page_size()];
        unsigned char reference_buffer_written[ssd_config->get_page_size()];
        unsigned char reference_buffer_erased[ssd_config->get_page_size()];
        memset(reference_buffer_written, 0x00, ssd_config->get_page_size());
        memset(reference_buffer_erased, 0xFF, ssd_config->get_page_size());

        for (size_t page = 0; page < ssd_config->get_page_nb(); page += ssd_config->get_pages_per_block())
        {
            memset(buffer, 0x00, ssd_config->get_page_size());
            ASSERT_EQ(ONFI_PAGE_PROGRAM(g_device_index, page, 0, buffer, ssd_config->get_page_size(), &nprogrammed), ONFI_SUCCESS);
            ASSERT_EQ(nprogrammed, ssd_config->get_page_size());
            AssertStatusRegisterOk();
            // set to value opposite of expected value to read
            memset(buffer, 0xFF, ssd_config->get_page_size());
            ASSERT_EQ(ONFI_READ(g_device_index, page, 0, buffer, ssd_config->get_page_size(), &nread), ONFI_SUCCESS);
            ASSERT_EQ(memcmp(reference_buffer_written, buffer, ssd_config->get_page_size()), 0);
            ASSERT_EQ(ONFI_BLOCK_ERASE(g_device_index, page), ONFI_SUCCESS);
            AssertStatusRegisterOk();
            // set to value opposite of expected value to read
            memset(buffer, 0x00, ssd_config->get_page_size());
            ASSERT_EQ(ONFI_READ(g_device_index, page, 0, buffer, ssd_config->get_page_size(), &nread), ONFI_SUCCESS);
            ASSERT_EQ(memcmp(reference_buffer_erased, buffer, ssd_config->get_page_size()), 0);
        }
    }

    /* ========== ONFI_RESET tests ========== */

    TEST_P(OnfiCommandsTest, InvalidDeviceIndexResetOperationFails)
    {
        ASSERT_EQ(ONFI_RESET(INVALID_DEVICE_INDEX), ONFI_FAILURE);
    }

    TEST_P(OnfiCommandsTest, StatusRegisterFailValuesResetAfterResetOperation)
    {
        onfi_status_reg_t status;

        ASSERT_EQ(ONFI_PAGE_PROGRAM(g_device_index, 0, 0, NULL, 0, NULL), ONFI_FAILURE);
        ASSERT_EQ(ONFI_READ_STATUS(g_device_index, &status), ONFI_SUCCESS);
        ASSERT_EQ(status.FAIL, 1);
        ASSERT_EQ(status.FAILC, 0);

        ASSERT_EQ(ONFI_PAGE_PROGRAM(g_device_index, 0, 0, NULL, 0, NULL), ONFI_FAILURE);
        ASSERT_EQ(ONFI_READ_STATUS(g_device_index, &status), ONFI_SUCCESS);
        ASSERT_EQ(status.FAIL, 1);
        ASSERT_EQ(status.FAILC, 1);

        ASSERT_EQ(ONFI_RESET(g_device_index), ONFI_SUCCESS);
        ASSERT_EQ(ONFI_READ_STATUS(g_device_index, &status), ONFI_SUCCESS);
        ASSERT_EQ(status.FAIL, 0);
        ASSERT_EQ(status.FAILC, 0);
        ASSERT_EQ(status.RDY, 1);
        ASSERT_EQ(status.ARDY, 1);
    }

    /* ========== ONFI_READ_PARAMETER_PAGE tests ========== */

    TEST_P(OnfiCommandsTest, ReadParameterPageInvalidDeviceIndexFails)
    {
        onfi_param_page_t param_page;
        ASSERT_EQ(ONFI_READ_PARAMETER_PAGE(INVALID_DEVICE_INDEX, 0, (uint8_t *)&param_page, sizeof(onfi_param_page_t)), ONFI_FAILURE);
    }

    TEST_P(OnfiCommandsTest, ReadParameterPageTimingModeNonzeroFails)
    {
        onfi_param_page_t param_page;

        ASSERT_EQ(ONFI_READ_PARAMETER_PAGE(g_device_index, 1, (uint8_t *)&param_page, sizeof(onfi_param_page_t)), ONFI_FAILURE);
    }

    TEST_P(OnfiCommandsTest, ReadParameterPageNullBufferFails)
    {
        ASSERT_EQ(ONFI_READ_PARAMETER_PAGE(g_device_index, 0, NULL, sizeof(onfi_param_page_t)), ONFI_FAILURE);
    }

    TEST_P(OnfiCommandsTest, ReadParameterPageBufferSizeZeroFails)
    {
        onfi_param_page_t param_page;

        ASSERT_EQ(ONFI_READ_PARAMETER_PAGE(g_device_index, 0, (uint8_t *)&param_page, 0), ONFI_FAILURE);
    }

    TEST_P(OnfiCommandsTest, ReadParameterPageValidArgumentsSuccess)
    {
        onfi_param_page_t param_page;

        ASSERT_EQ(ONFI_READ_PARAMETER_PAGE(g_device_index, 0, (uint8_t *)&param_page, sizeof(onfi_param_page_t)), ONFI_SUCCESS);

        ASSERT_EQ(param_page.mem_org_block.data_bytes_per_page, GET_PAGE_SIZE(g_device_index));
        ASSERT_EQ(param_page.mem_org_block.pages_per_block, GET_PAGE_NB(g_device_index));
    }

    TEST_P(OnfiCommandsTest, ReadParameterPageSmallBuffer)
    {
        onfi_param_page_t param_page;

        ASSERT_EQ(ONFI_READ_PARAMETER_PAGE(g_device_index, 0, (uint8_t *)&param_page, 8), ONFI_SUCCESS);

        ASSERT_EQ(memcmp(param_page.signature, "ONFI", 4), 0);
        ASSERT_GE(param_page.revision, 0x0001);
    }

    TEST_P(OnfiCommandsTest, ReadParameterPageBigBuffer)
    {
        unsigned repetitions = 4;
        onfi_param_page_t param_page[repetitions];

        ASSERT_EQ(ONFI_READ_PARAMETER_PAGE(g_device_index, 0, (uint8_t *)&param_page, sizeof(onfi_param_page_t) * repetitions), ONFI_SUCCESS);

        for (unsigned i = 0; i < repetitions - 1; ++i)
        {
            ASSERT_EQ(memcmp(param_page + i, param_page + (i + 1), sizeof(onfi_param_page_t)), 0);
        }
    }

    TEST_P(OnfiCommandsTest, ParameterPageCorrectConstants) {
        onfi_param_page_t param_page;

        ASSERT_EQ(ONFI_READ_PARAMETER_PAGE(g_device_index, 0, (uint8_t *)&param_page, sizeof(onfi_param_page_t)), ONFI_SUCCESS);
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

    TEST_P(OnfiCommandsTest, ParameterPageCrcCorrect) {
        onfi_param_page_t param_page;

        ASSERT_EQ(ONFI_READ_PARAMETER_PAGE(g_device_index, 0, (uint8_t *)&param_page, sizeof(onfi_param_page_t)), ONFI_SUCCESS);
        ASSERT_EQ(param_page.vendor_block.integrity_crc, _ONFI_CRC16((uint8_t *)&param_page, sizeof(param_page) - sizeof(param_page.vendor_block.integrity_crc)));
    }

    /* ========== ONFI_READ_ID tests ========== */

    TEST_P(OnfiCommandsTest, InvalidDeviceIndexReadIDOperationFails)
    {
        uint8_t ids[2] = {0};
        ASSERT_EQ(ONFI_READ_ID(INVALID_DEVICE_INDEX, JEDEC_ID_ADDR, ids, sizeof(ids)), ONFI_FAILURE);
    }

    TEST_P(OnfiCommandsTest, ReadIDNullBufferParameter)
    {
        ASSERT_EQ(ONFI_READ_ID(g_device_index, JEDEC_ID_ADDR, NULL, 0), ONFI_FAILURE);
    }

    TEST_P(OnfiCommandsTest, ReadIDInvalidAddressParameter)
    {
        uint8_t buff[2] = {0};
        ASSERT_EQ(ONFI_READ_ID(g_device_index, 1, buff, sizeof(buff)), ONFI_FAILURE);
    }

    TEST_P(OnfiCommandsTest, ReadIDReadCorrectJedecID)
    {
        uint8_t ids[2] = {0};
        ASSERT_EQ(ONFI_READ_ID(g_device_index, JEDEC_ID_ADDR, ids, sizeof(ids)), ONFI_SUCCESS);

        onfi_param_page_t param_page;
        ASSERT_EQ(ONFI_READ_PARAMETER_PAGE(g_device_index, 0, (uint8_t *)&param_page, sizeof(onfi_param_page_t)), ONFI_SUCCESS);

        ASSERT_EQ(ids[0], param_page.manufacturer_info_block.jedec_manufacturer);
        ASSERT_EQ(ids[1], 0x10);
    }

    TEST_P(OnfiCommandsTest, ReadIDReadJedecIDIntoSmallerBuffer)
    {
        uint8_t ids[2] = {0};
        memset(ids, 0, sizeof(ids));
        ASSERT_EQ(ONFI_READ_ID(g_device_index, JEDEC_ID_ADDR, ids, sizeof(ids) - 1), ONFI_SUCCESS);

        onfi_param_page_t param_page;
        ASSERT_EQ(ONFI_READ_PARAMETER_PAGE(g_device_index, 0, (uint8_t *)&param_page, sizeof(onfi_param_page_t)), ONFI_SUCCESS);

        ASSERT_EQ(ids[0], param_page.manufacturer_info_block.jedec_manufacturer);
        ASSERT_EQ(ids[1], 0);
    }

    TEST_P(OnfiCommandsTest, ReadIDReadJedecIDIntoBiggerBuffer)
    {
        uint8_t ids[3] = {0};
        memset(ids, 0, sizeof(ids));
        ASSERT_EQ(ONFI_READ_ID(g_device_index, JEDEC_ID_ADDR, ids, sizeof(ids)), ONFI_SUCCESS);

        onfi_param_page_t param_page;
        ASSERT_EQ(ONFI_READ_PARAMETER_PAGE(g_device_index, 0, (uint8_t *)&param_page, sizeof(onfi_param_page_t)), ONFI_SUCCESS);

        ASSERT_EQ(ids[0], param_page.manufacturer_info_block.jedec_manufacturer);
        ASSERT_EQ(ids[1], 0x10);
        ASSERT_EQ(ids[2], 0);
    }

    TEST_P(OnfiCommandsTest, ReadIDReadOnfiSignature)
    {
        uint8_t onfi_signature[4] = {0};
        ASSERT_EQ(ONFI_READ_ID(g_device_index, ONFI_SIGNATURE_ADDR, onfi_signature, sizeof(onfi_signature)), ONFI_SUCCESS);

        ASSERT_EQ(onfi_signature[0], 'O');
        ASSERT_EQ(onfi_signature[1], 'N');
        ASSERT_EQ(onfi_signature[2], 'F');
        ASSERT_EQ(onfi_signature[3], 'I');
    }

    TEST_P(OnfiCommandsTest, ReadIDReadOnfiSignatureIntoSmallerBuffer)
    {
        uint8_t onfi_signature[4] = {0};
        memset(onfi_signature, 0, sizeof(onfi_signature));
        ASSERT_EQ(ONFI_READ_ID(g_device_index, ONFI_SIGNATURE_ADDR, onfi_signature, sizeof(onfi_signature) - 1), ONFI_SUCCESS);

        ASSERT_EQ(onfi_signature[0], 'O');
        ASSERT_EQ(onfi_signature[1], 'N');
        ASSERT_EQ(onfi_signature[2], 'F');
        ASSERT_EQ(onfi_signature[3], 0);
    }

    TEST_P(OnfiCommandsTest, ReadIDReadOnfiSignatureIntoBiggerBuffer)
    {
        uint8_t onfi_signature[5] = {0};
        memset(onfi_signature, 0, sizeof(onfi_signature));
        ASSERT_EQ(ONFI_READ_ID(g_device_index, ONFI_SIGNATURE_ADDR, onfi_signature, sizeof(onfi_signature)), ONFI_SUCCESS);

        ASSERT_EQ(onfi_signature[0], 'O');
        ASSERT_EQ(onfi_signature[1], 'N');
        ASSERT_EQ(onfi_signature[2], 'F');
        ASSERT_EQ(onfi_signature[3], 'I');
        ASSERT_EQ(onfi_signature[4], 0);
    }

} // namespace
