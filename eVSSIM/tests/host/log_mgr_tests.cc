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
#include "logging_backend.h"
#include "logging_parser.h"
#include "logging_rt_analyzer.h"
}
bool g_ci_mode = false;

#include "rt_analyzer_subscriber.h"

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

        // push the different sizes of the buffer, in bytes
        list.push_back(1024);
        list.push_back(8192);

        return list;
    }

    INSTANTIATE_TEST_CASE_P(LoggerSize, LogMgrUnitTest, ::testing::ValuesIn(GetParams()));

    /*
     * Test reading back a written string:
     * - make sure normal writing works
     * - make sure normal reading works
     */
    TEST_P(LogMgrUnitTest, NormalStringWriteRead) {
        ASSERT_EQ(0, logger_write(_logger, (Byte*) TEST_STRING, sizeof(TEST_STRING)));
        char res[2 * sizeof(TEST_STRING)];
        ASSERT_EQ(sizeof(TEST_STRING), logger_read(_logger, (Byte*) res, sizeof(TEST_STRING)));
        ASSERT_STREQ(res, TEST_STRING);
    }

    /*
     * Test reading back a string which was written on the boundary of the buffer:
     * - make sure writing at buffer boundary works
     * - make sure reading from buffer boundary works
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
     * Test writing a string bigger than the buffer:
     * - make sure writing a string bigger than the buffer returns non-zero (a warning)
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
     * Test filling up the buffer:
     * - make sure filling up the buffer returns non-zero (a warning)
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
     * Test reading the logger after successive writes:
     * - make sure the buffer works as a queue, and not a stack
     *   (read in the same order of the write)
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
     * Test reading before writing:
     * - make sure reading before any writing doesn't work
     * - make sure reading after writing does work
     */
    TEST_P(LogMgrUnitTest, ReadBeforeWrite) {
        Byte placeholder = 'J';
        Byte res;
        ASSERT_EQ(0, logger_read(_logger, &res, 1));
        ASSERT_EQ(0, logger_write(_logger, &placeholder, 1));
        ASSERT_EQ(1, logger_read(_logger, &res, 1));
        ASSERT_EQ(placeholder, res);
    }

    /* Unit tests for the different logs */

    /*
     * Test writing and reading a physical page read log
     */
    TEST_P(LogMgrUnitTest, PhysicalPageRead) {
        PhysicalPageReadLog log = {
                .channel = 3,
                .block = 80,
                .page = 123
        };
        LOG_PHYSICAL_PAGE_READ(_logger, log);
        ASSERT_EQ(PHYSICAL_PAGE_READ_LOG_UID, next_log_type(_logger));
        PhysicalPageReadLog res = NEXT_PHYSICAL_PAGE_READ_LOG(_logger);
        ASSERT_EQ(log.channel, res.channel);
        ASSERT_EQ(log.block, res.block);
        ASSERT_EQ(log.page, res.page);
    }

    /*
     * Test writing and reading a physical page write log
     */
    TEST_P(LogMgrUnitTest, PhysicalPageWrite) {
        PhysicalPageWriteLog log = {
                .channel = 15,
                .block = 63,
                .page = 50
        };
        LOG_PHYSICAL_PAGE_WRITE(_logger, log);
        ASSERT_EQ(PHYSICAL_PAGE_WRITE_LOG_UID, next_log_type(_logger));
        PhysicalPageWriteLog res = NEXT_PHYSICAL_PAGE_WRITE_LOG(_logger);
        ASSERT_EQ(log.channel, res.channel);
        ASSERT_EQ(log.block, res.block);
        ASSERT_EQ(log.page, res.page);
    }

    /*
     * Test writing and reading a logical page write log
     */
    TEST_P(LogMgrUnitTest, LogicalPageWrite) {
        LogicalPageWriteLog log = {
                .channel = 2,
                .block = 260,
                .page = 3
        };
        LOG_LOGICAL_PAGE_WRITE(_logger, log);
        ASSERT_EQ(LOGICAL_PAGE_WRITE_LOG_UID, next_log_type(_logger));
        LogicalPageWriteLog res = NEXT_LOGICAL_PAGE_WRITE_LOG(_logger);
        ASSERT_EQ(log.channel, res.channel);
        ASSERT_EQ(log.block, res.block);
        ASSERT_EQ(log.page, res.page);
    }

    /*
     * Test writing and reading a garbage collection log
     */
    TEST_P(LogMgrUnitTest, GarbageCollection) {
        GarbageCollectionLog log = { };
        LOG_GARBAGE_COLLECTION(_logger, log);
        ASSERT_EQ(GARBAGE_COLLECTION_LOG_UID, next_log_type(_logger));
        // test that NEXT_GARBAGE_COLLECTION_LOG actually does nothing,
        // due to the fact that the struct is empty
        Byte placeholder;
        ASSERT_EQ(0, logger_read(_logger, &placeholder, 1));
        NEXT_GARBAGE_COLLECTION_LOG(_logger);
        ASSERT_EQ(0, logger_read(_logger, &placeholder, 1));
    }

    /* Real Time Analyzer Tests */

    /*
     * Do a simple test of the real time log analyzer
     */
    TEST_P(LogMgrUnitTest, BasicRTAnalyzer) {
        RTLogAnalyzer* analyzer = rt_log_analyzer_init(_logger);
        rt_subscriber::subscribe(analyzer);
        rt_subscriber::write();
        rt_subscriber::read();
        rt_log_analyzer_free(analyzer, 0);
    }
} //namespace
