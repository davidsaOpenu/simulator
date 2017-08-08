/*
 * Copyright 2017 The Open University of Israel
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

extern "C" {

#include "common.h"
#include "logging_parser.h"
}
bool g_ci_mode = false;

#define GTEST_DONT_DEFINE_FAIL 1
#include <gtest/gtest.h>

#include <fstream>
#include <cstdio>
#include <cstdlib>

int main(int argc, char **argv) {
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--ci") == 0) {
            g_ci_mode = true;
        }
    }
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

using namespace std;

namespace {
    class LogMgrUnitTest : public ::testing::TestWithParam<size_t> {
        public:
            const static char TEST_STRING[];

            virtual void SetUp() {
                size_t logger_size = GetParam();
                _logger = logger_init(logger_size);
            }
            virtual void TearDown() {
                logger_free(_logger);
            }
        protected:
            Logger* _logger;
    };
    const char LogMgrUnitTest::TEST_STRING[] = "Test Me Please";


    /*
     * Calculate the different buffer sizes for the logger
     */
    std::vector<size_t> GetParams() {
        std::vector<size_t> list;

        if (g_ci_mode) {
            printf("Running in CI mode\n");
            list.push_back(16);
            list.push_back(32);
            list.push_back(64);
        }
        else {
            list.push_back(1024);
        }
        return list;
    }

    INSTANTIATE_TEST_CASE_P(LoggerSize, LogMgrUnitTest, ::testing::ValuesIn(GetParams()));

    /*
     * Test reading back a written string
     */
    TEST_P(LogMgrUnitTest, NormalStringWriteRead) {
        ASSERT_EQ(0, logger_write(_logger, (Byte*) TEST_STRING, sizeof(TEST_STRING)));
        char res[2 * sizeof(TEST_STRING)];
        ASSERT_EQ(sizeof(TEST_STRING), logger_read(_logger, (Byte*) res, sizeof(TEST_STRING)));
        ASSERT_STREQ(res, TEST_STRING);
    }

    /*
     * Test reading back a string which was written on the boundary of the buffer
     */
    TEST_P(LogMgrUnitTest, WrapStringWriteRead) {
        // fill the buffer
        Byte placeholder = 'x';
        for (unsigned int i = 0; i < _logger->buffer_size - (sizeof(TEST_STRING) / 2); i++) {
            ASSERT_EQ(0, logger_write(_logger, &placeholder, 1));
        }
        // increment the reader
        for (unsigned int i = 0; i < _logger->buffer_size - (sizeof(TEST_STRING) / 2); i++) {
            ASSERT_EQ(1, logger_read(_logger, &placeholder, 1));
        }
        // write the string and read it back
        ASSERT_EQ(0, logger_write(_logger, (Byte*) TEST_STRING, sizeof(TEST_STRING)));
        char res[2 * sizeof(TEST_STRING)];
        ASSERT_EQ(sizeof(TEST_STRING), logger_read(_logger, (Byte*) res, sizeof(TEST_STRING)));
        ASSERT_STREQ(res, TEST_STRING);
    }

    /*
     * Test writing a string bigger than the buffer
     */
    TEST_P(LogMgrUnitTest, HugeStringWarning) {
        // create the huge string
        size_t huge_size = _logger->buffer_size;
        Byte* huge_string = new Byte[huge_size];
        memset(huge_string, '?', huge_size);
        // log the huge string
        ASSERT_NE(0, logger_write(_logger, huge_string, huge_size));
        // free the huge string
        delete [] huge_string;
    }

    /*
     * Test filling up the buffer
     */
    TEST_P(LogMgrUnitTest, FullBuffer) {
        Byte offset[4];
        Byte placeholder = 'y';
        // write and read the offset
        ASSERT_EQ(0, logger_write(_logger, offset, sizeof(offset)));
        ASSERT_EQ(sizeof(offset), logger_read(_logger, offset, sizeof(offset)));
        // almost fill the buffer
        for (unsigned int i = 0; i < _logger->buffer_size - 1; i++)
            ASSERT_EQ(0, logger_write(_logger, &placeholder, 1));
        // try to fill the buffer completely (one slot empty means a full buffer)
        ASSERT_NE(0, logger_write(_logger, &placeholder, 1));
    }

    /*
     * Test reading the logger after successive writes
     */
    TEST_P(LogMgrUnitTest, SuccessiveWrites) {
        Byte first = 'X';
        Byte second = 'Y';
        Byte first_res;
        Byte second_res;
        ASSERT_EQ(0, logger_write(_logger, &first, 1));
        ASSERT_EQ(0, logger_write(_logger, &second, 1));
        ASSERT_EQ(1, logger_read(_logger, &first_res, 1));
        ASSERT_EQ(1, logger_read(_logger, &second_res, 1));
        ASSERT_EQ(first, first_res);
        ASSERT_EQ(second, second_res);
    }

    /*
     * Test reading before writing
     */
    TEST_P(LogMgrUnitTest, ReadBeforeWrite) {
        Byte placeholder = 'J';
        Byte res;
        ASSERT_EQ(0, logger_read(_logger, &res, 1));
        ASSERT_EQ(0, logger_write(_logger, &placeholder, 1));
        ASSERT_EQ(1, logger_read(_logger, &res, 1));
        ASSERT_EQ(placeholder, res);
    }

    /*
     * Test writing and reading a register write log
     */
    TEST_P(LogMgrUnitTest, LogRegWrite) {
        const int reg = 3;
        LOG_REG_WRITE(_logger, (RegWriteLog) { reg });
        ASSERT_EQ(REG_WRITE_LOG_UID, next_log_type(_logger));
        RegWriteLog log;
        NEXT_LOG(_logger, log);
        ASSERT_EQ(log.reg, reg);
    }

    /*
     * Test writing and reading a register read log
     */
    TEST_P(LogMgrUnitTest, LogRegRead) {
        const int reg = 7;
        LOG_REG_READ(_logger, (RegReadLog) { reg });
        ASSERT_EQ(REG_READ_LOG_UID, next_log_type(_logger));
        RegReadLog log;
        NEXT_LOG(_logger, log);
        ASSERT_EQ(log.reg, reg);
    }
} //namespace
