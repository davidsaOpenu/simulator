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

extern RTLogStatistics *rt_log_stats;
extern LogServer log_server;

// for performance.
#define CHECK_THRESHOLD 0.1 // 10%

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
            ASSERT_EQ(_FTL_CREATE(g_device_index), FTL_SUCCESS);
            ASSERT_EQ(_ONFI_INIT(), ONFI_SUCCESS);
        }

        virtual void TearDown()
        {
            BaseTest::TearDown(false);
            TERM_LOG_MANAGER(g_device_index);
            remove(GET_FILE_NAME(g_device_index));
            TERM_SSD_CONFIG();
        }

        void AssertStatusRegisterOk()
        {
            onfi_status_reg_t status;
            ASSERT_EQ(ONFI_READ_STATUS(&status), ONFI_SUCCESS);
            ASSERT_EQ(status.FAIL, 0);
        }
    };

    std::vector<SSDConf *> GetTestParams()
    {
        std::vector<SSDConf *> ssd_configs;
        ssd_configs.push_back(new SSDConf(pow(2, 5), 1));
        ssd_configs.push_back(new SSDConf(pow(2, 5), 2));
        return ssd_configs;
    }

    INSTANTIATE_TEST_CASE_P(DiskSize, OnfiCommandsTest, ::testing::ValuesIn(GetTestParams()));

    /* ========== ONFI_READ_STATUS tests ========== */

    TEST_P(OnfiCommandsTest, NullRegisterReadStatusFails)
    {
        ASSERT_EQ(ONFI_READ_STATUS(NULL), ONFI_FAILURE);
    }

    TEST_P(OnfiCommandsTest, StatusRegisterInitializedWithSuccess)
    {
        onfi_status_reg_t status;
        ASSERT_EQ(ONFI_READ_STATUS(&status), ONFI_SUCCESS);
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

        ASSERT_EQ(ONFI_PAGE_PROGRAM(0, 0, NULL, 0, NULL), ONFI_FAILURE);
        ASSERT_EQ(ONFI_READ_STATUS(&status), ONFI_SUCCESS);
        ASSERT_EQ(status.FAIL, 1);
        ASSERT_EQ(status.FAILC, 0);

        ASSERT_EQ(ONFI_PAGE_PROGRAM(0, 0, NULL, 0, NULL), ONFI_FAILURE);
        ASSERT_EQ(ONFI_READ_STATUS(&status), ONFI_SUCCESS);
        ASSERT_EQ(status.FAIL, 1);
        ASSERT_EQ(status.FAILC, 1);

        ASSERT_EQ(ONFI_PAGE_PROGRAM(0, 0, &data, sizeof(data), &nprogrammed), ONFI_SUCCESS);
        ASSERT_EQ(ONFI_READ_STATUS(&status), ONFI_SUCCESS);
        ASSERT_EQ(status.FAIL, 0);
        ASSERT_EQ(status.FAILC, 1);

        ASSERT_EQ(ONFI_PAGE_PROGRAM(0, 0, &data, sizeof(data), &nprogrammed), ONFI_SUCCESS);
        ASSERT_EQ(ONFI_READ_STATUS(&status), ONFI_SUCCESS);
        ASSERT_EQ(status.FAIL, 0);
        ASSERT_EQ(status.FAILC, 0);
    }

    TEST_P(OnfiCommandsTest, StatusRegisterFailValuesCorrectAfterBlockEraseOperation)
    {
        SSDConf *ssd_config = base_test_get_ssd_config();
        onfi_status_reg_t status;

        ASSERT_EQ(ONFI_BLOCK_ERASE(ssd_config->get_block_nb()), ONFI_FAILURE);
        ASSERT_EQ(ONFI_READ_STATUS(&status), ONFI_SUCCESS);
        ASSERT_EQ(status.FAIL, 1);
        ASSERT_EQ(status.FAILC, 0);

        ASSERT_EQ(ONFI_BLOCK_ERASE(ssd_config->get_block_nb()), ONFI_FAILURE);
        ASSERT_EQ(ONFI_READ_STATUS(&status), ONFI_SUCCESS);
        ASSERT_EQ(status.FAIL, 1);
        ASSERT_EQ(status.FAILC, 1);

        ASSERT_EQ(ONFI_BLOCK_ERASE(0), ONFI_SUCCESS);
        ASSERT_EQ(ONFI_READ_STATUS(&status), ONFI_SUCCESS);
        ASSERT_EQ(status.FAIL, 0);
        ASSERT_EQ(status.FAILC, 1);

        ASSERT_EQ(ONFI_BLOCK_ERASE(0), ONFI_SUCCESS);
        ASSERT_EQ(ONFI_READ_STATUS(&status), ONFI_SUCCESS);
        ASSERT_EQ(status.FAIL, 0);
        ASSERT_EQ(status.FAILC, 0);
    }

    /* ========== ONFI_READ tests ========== */

    TEST_P(OnfiCommandsTest, NullBufferReadFails)
    {
        SSDConf *ssd_config = base_test_get_ssd_config();

        size_t nread = 0;
        ASSERT_EQ(ONFI_READ(0, 0, NULL, ssd_config->get_page_size(), &nread), ONFI_FAILURE);
    }

    TEST_P(OnfiCommandsTest, NullReadAmountReadFails)
    {
        SSDConf *ssd_config = base_test_get_ssd_config();

        unsigned char buffer[ssd_config->get_page_size()];

        ASSERT_EQ(ONFI_READ(0, 0, buffer, ssd_config->get_page_size(), NULL), ONFI_FAILURE);
    }

    TEST_P(OnfiCommandsTest, OutOfBoundsRowAddressReadFails)
    {
        SSDConf *ssd_config = base_test_get_ssd_config();

        unsigned char buffer[ssd_config->get_page_size()];
        size_t nread = 0;

        ASSERT_EQ(ONFI_READ(ssd_config->get_page_nb(), 0, buffer, ssd_config->get_page_size(), &nread), ONFI_FAILURE);
    }

    TEST_P(OnfiCommandsTest, OutOfBoundsColumnAddressReadFails)
    {
        SSDConf *ssd_config = base_test_get_ssd_config();

        unsigned char buffer[ssd_config->get_page_size()];
        size_t nread = 0;

        ASSERT_EQ(ONFI_READ(0, ssd_config->get_page_size(), buffer, 1, &nread), ONFI_FAILURE);
    }

    TEST_P(OnfiCommandsTest, ReadAllSuccess)
    {
        SSDConf *ssd_config = base_test_get_ssd_config();

        size_t nread = 0;
        unsigned char buffer[ssd_config->get_page_size()];
        unsigned char reference_buffer[ssd_config->get_page_size()];
        memset(reference_buffer, 0xFF, ssd_config->get_page_size());

        for (size_t page = 0; page < ssd_config->get_page_nb(); ++page)
        {
            memset(buffer, 0x00, ssd_config->get_page_size());
            ASSERT_EQ(ONFI_READ(page, 0, buffer, ssd_config->get_page_size(), &nread), ONFI_SUCCESS);
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

        ASSERT_EQ(ONFI_READ(0, column, buffer, read_size, &nread), ONFI_SUCCESS);
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

        ASSERT_EQ(ONFI_READ(0, column, buffer, ssd_config->get_page_size(), &nread), ONFI_SUCCESS);
        ASSERT_EQ(nread, expected_nread);
        ASSERT_EQ(memcmp(reference_buffer, buffer, nread), 0);
    }

    /* ========== ONFI_PAGE_PROGRAM tests ========== */

    TEST_P(OnfiCommandsTest, NullBufferPageProgramFails)
    {
        SSDConf *ssd_config = base_test_get_ssd_config();

        size_t nprogrammed = 0;
        ASSERT_EQ(ONFI_PAGE_PROGRAM(0, 0, NULL, ssd_config->get_page_size(), &nprogrammed), ONFI_FAILURE);
    }

    TEST_P(OnfiCommandsTest, NullProgramAmountPageProgramFails)
    {
        SSDConf *ssd_config = base_test_get_ssd_config();

        unsigned char buffer[ssd_config->get_page_size()];

        ASSERT_EQ(ONFI_PAGE_PROGRAM(0, 0, buffer, ssd_config->get_page_size(), NULL), ONFI_FAILURE);
    }

    TEST_P(OnfiCommandsTest, OutOfBoundsRowAddressPageProgramFails)
    {
        SSDConf *ssd_config = base_test_get_ssd_config();

        unsigned char buffer[ssd_config->get_page_size()];
        size_t nprogrammed = 0;

        ASSERT_EQ(ONFI_PAGE_PROGRAM(ssd_config->get_page_nb(), 0, buffer, ssd_config->get_page_size(), &nprogrammed), ONFI_FAILURE);
    }

    TEST_P(OnfiCommandsTest, OutOfBoundsColumnAddressPageProgramFails)
    {
        SSDConf *ssd_config = base_test_get_ssd_config();

        unsigned char buffer[ssd_config->get_page_size()];
        size_t nprogrammed = 0;

        ASSERT_EQ(ONFI_PAGE_PROGRAM(0, ssd_config->get_page_size(), buffer, 1, &nprogrammed), ONFI_FAILURE);
    }

    TEST_P(OnfiCommandsTest, PageProgramAllSuccess)
    {
        SSDConf *ssd_config = base_test_get_ssd_config();

        size_t nprogrammed = 0;
        size_t nread = 0;
        unsigned char buffer[ssd_config->get_page_size()];
        unsigned char reference_buffer[ssd_config->get_page_size()];
        memset(reference_buffer, 0x00, ssd_config->get_page_size());

        for (size_t page = 0; page < ssd_config->get_page_nb(); ++page)
        {
            memset(buffer, 0x00, ssd_config->get_page_size());
            ASSERT_EQ(ONFI_PAGE_PROGRAM(page, 0, buffer, ssd_config->get_page_size(), &nprogrammed), ONFI_SUCCESS);
            ASSERT_EQ(nprogrammed, ssd_config->get_page_size());
            memset(buffer, 0xFF, ssd_config->get_page_size());
            ASSERT_EQ(ONFI_READ(page, 0, buffer, ssd_config->get_page_size(), &nread), ONFI_SUCCESS);
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

        ASSERT_EQ(ONFI_PAGE_PROGRAM(0, column, buffer, program_size, &nprogrammed), ONFI_SUCCESS);
        ASSERT_EQ(nprogrammed, program_size);
        memset(buffer, 0xFF, ssd_config->get_page_size());
        ASSERT_EQ(ONFI_READ(0, column, buffer, program_size, &nread), ONFI_SUCCESS);
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

        ASSERT_EQ(ONFI_PAGE_PROGRAM(0, column, buffer, ssd_config->get_page_size(), &nprogrammed), ONFI_SUCCESS);
        ASSERT_EQ(nprogrammed, expected_nprogrammed);
        memset(buffer, 0xFF, ssd_config->get_page_size());
        ASSERT_EQ(ONFI_READ(0, column, buffer, ssd_config->get_page_size(), &nread), ONFI_SUCCESS);
        ASSERT_EQ(memcmp(reference_buffer, buffer, nprogrammed), 0);
        AssertStatusRegisterOk();
    }

    /* ========== ONFI_BLOCK_ERASE tests ========== */

    TEST_P(OnfiCommandsTest, OutOfBoundsRowAddressBlockEraseFails)
    {
        SSDConf *ssd_config = base_test_get_ssd_config();

        ASSERT_EQ(ONFI_BLOCK_ERASE(ssd_config->get_page_nb()), ONFI_FAILURE);
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
            ASSERT_EQ(ONFI_PAGE_PROGRAM(page, 0, buffer, ssd_config->get_page_size(), &nprogrammed), ONFI_SUCCESS);
            ASSERT_EQ(nprogrammed, ssd_config->get_page_size());
            AssertStatusRegisterOk();
            // set to value opposite of expected value to read
            memset(buffer, 0xFF, ssd_config->get_page_size());
            ASSERT_EQ(ONFI_READ(page, 0, buffer, ssd_config->get_page_size(), &nread), ONFI_SUCCESS);
            ASSERT_EQ(memcmp(reference_buffer_written, buffer, ssd_config->get_page_size()), 0);
            ASSERT_EQ(ONFI_BLOCK_ERASE(page), ONFI_SUCCESS);
            AssertStatusRegisterOk();
            // set to value opposite of expected value to read
            memset(buffer, 0x00, ssd_config->get_page_size());
            ASSERT_EQ(ONFI_READ(page, 0, buffer, ssd_config->get_page_size(), &nread), ONFI_SUCCESS);
            ASSERT_EQ(memcmp(reference_buffer_erased, buffer, ssd_config->get_page_size()), 0);
        }
    }

    /* ========== ONFI_RESET tests ========== */

    TEST_P(OnfiCommandsTest, StatusRegisterFailValuesResetAfterResetOperation)
    {
        onfi_status_reg_t status;

        ASSERT_EQ(ONFI_PAGE_PROGRAM(0, 0, NULL, 0, NULL), ONFI_FAILURE);
        ASSERT_EQ(ONFI_READ_STATUS(&status), ONFI_SUCCESS);
        ASSERT_EQ(status.FAIL, 1);
        ASSERT_EQ(status.FAILC, 0);

        ASSERT_EQ(ONFI_PAGE_PROGRAM(0, 0, NULL, 0, NULL), ONFI_FAILURE);
        ASSERT_EQ(ONFI_READ_STATUS(&status), ONFI_SUCCESS);
        ASSERT_EQ(status.FAIL, 1);
        ASSERT_EQ(status.FAILC, 1);

        ASSERT_EQ(ONFI_RESET(), ONFI_SUCCESS);
        ASSERT_EQ(ONFI_READ_STATUS(&status), ONFI_SUCCESS);
        ASSERT_EQ(status.FAIL, 0);
        ASSERT_EQ(status.FAILC, 0);
        ASSERT_EQ(status.RDY, 1);
        ASSERT_EQ(status.ARDY, 1);
    }

} // namespace
