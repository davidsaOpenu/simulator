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
}
extern bool g_monitor_mode;
extern bool g_server_mode;

#include "base_emulator_tests.h"
#include "rt_analyzer_subscriber.h"
#include "log_manager_subscriber.h"
#include "logging_parser.h"

#include <pthread.h>
#include <unistd.h>

using namespace std;


namespace log_mgr_tests {
    class LogMgrUnitTest : public BaseTest {
        public:
            const static char TEST_STRING[];

            virtual void SetUp() {
                BaseTest::SetUp();
                INIT_SSD_CONFIG();

                if (g_server_mode) {
                    log_server_init();
                    pthread_create(&_server, NULL, log_server_run, NULL);
                    printf("Server opened\n");
                    printf("Browse to http://127.0.0.1:%d/ to see the statistics\n",
                            LOG_SERVER_PORT);
                }
                SSDConf* ssd_config = base_test_get_ssd_config();

                _logger = logger_init(ssd_config->get_logger_size());
            }
            virtual void TearDown() {
                logger_free(_logger);
                BaseTest::TearDown();
                if (g_monitor_mode) {
                    printf("Waiting for monitor to close...\n");
                    pthread_join(_monitor, NULL);
                    printf("Monitor closed\n");
                }

                if (g_server_mode) {
                    printf("Waiting for server to close...\n");
                    pthread_join(_server, NULL);
                    printf("Server closed\n");
                    log_server_free();
                }
            }
        protected:
            Logger_Pool* _logger;
            pthread_t _monitor;
            pthread_t _server;
    };

    const char LogMgrUnitTest::TEST_STRING[] = "Test Me Please";


    /**
     * Calculate the different buffer sizes for the logger
     * @return the different buffer sizes for the logger
     */
    std::vector<SSDConf*> GetTestParams() {
        std::vector<SSDConf*> ssd_configs;

        size_t page_size = 1048576;
        size_t sector_size = 1048576;
        size_t page_nb = 2;
        size_t flash_nb = 4;
        size_t block_nb = 8;
        size_t channel_nb = 4;

        std::vector<size_t> log_sizes;
        log_sizes.push_back(1);
        log_sizes.push_back(2);
        log_sizes.push_back(3);
        log_sizes.push_back(10);

        for (unsigned int i = 0; i < log_sizes.size(); i++) {
            SSDConf* config = new SSDConf(page_size, page_nb, sector_size, flash_nb, block_nb, channel_nb);
            config->set_logger_size(log_sizes[i]);
            ssd_configs.push_back(config);
        }

        return ssd_configs;
    }

    INSTANTIATE_TEST_CASE_P(LoggerSize, LogMgrUnitTest, ::testing::ValuesIn(GetTestParams()));


	TEST_P(LogMgrUnitTest, BasicRTAnalyzer) {
        elk_logger_writer_init();
        RTLogAnalyzer* analyzer = rt_log_analyzer_init(_logger, 0);
        rt_log_stats_init();
        rt_subscriber::subscribe(analyzer);
        if (g_server_mode)
            rt_log_analyzer_subscribe(analyzer, (MonitorHook) log_server_update, NULL);
        rt_subscriber::write();
        rt_subscriber::read();
        rt_log_analyzer_free(analyzer, 0);
        rt_log_stats_free();
        elk_logger_writer_free();
    }
    
    TEST_P(LogMgrUnitTest, BasicLogManager) {
        elk_logger_writer_init();
        LogManager* manager = log_manager_init();
        manager_subscriber::init(manager);
        if (g_server_mode)
            log_manager_subscribe(manager, (MonitorHook) log_server_update, NULL);
        manager_subscriber::run();
        manager_subscriber::free();
        log_manager_free(manager);
        elk_logger_writer_free();
    }

    /**
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
    /**
     * Test read before write on log boundry
     * Fill the log with data then read it and try to read
     * One more byte
     */
    TEST_P(LogMgrUnitTest, ReadBeforeWriteOnLogBoundry) {
        Byte placeholder = 'x';
        char res[5];

        for (unsigned int i = 0; i < ((LOG_SIZE)-1); i++)
            ASSERT_EQ(0, logger_write(_logger, &placeholder, 1));
        for (unsigned int i = 0; i < ((LOG_SIZE)-1); i++)
            ASSERT_EQ(1, logger_read(_logger, (Byte *)res, 1));
        ASSERT_EQ(0, logger_read(_logger, (Byte *)res, 5));
    }
    /**
     * Test filling all the allocated log's with test string and reading it back:
     * - make sure normal writing works
     * - make sure normal reading works
     */
    TEST_P(LogMgrUnitTest, NormalStringFillWriteRead) {
        char res[sizeof(TEST_STRING)];

        for (unsigned int i = 0;
                i < (unsigned int)((LOG_SIZE * _logger->number_of_allocated_logs)/sizeof(TEST_STRING));
                i++){
            ASSERT_EQ(0, logger_write(_logger, (Byte*) TEST_STRING, sizeof(TEST_STRING)));
        }

        for (unsigned int i = 0;
                i < (unsigned int)((LOG_SIZE * _logger->number_of_allocated_logs)/sizeof(TEST_STRING));
                i++)
        {
            ASSERT_EQ(sizeof(TEST_STRING), logger_read(_logger, (Byte*) res, sizeof(TEST_STRING)));
            ASSERT_STREQ(res, TEST_STRING);
            memset(res,0, sizeof(TEST_STRING));
        }
    }
    /**
     * Test logger_clean() function provided that RT Analyzer
     * hasn't read the logs yet. The expected result of this test
     * is that none of the logs will be cleand.
     * - make sure that logger_clean() doesn't clean any logs
     *   if log->rt_analyzer_done = false
     */
    TEST_P(LogMgrUnitTest, CleanRTAnalyzerNotDone)
    {
        Byte placeholder = 'x';
        int number_of_free_logs;

        // write full log
        for (unsigned long int i = 0; i < ((LOG_SIZE * _logger->number_of_allocated_logs)); i++) {
            ASSERT_EQ(0, logger_write(_logger, &placeholder, 1));
        }

        // save this number to make sure that logger clean function
        // didn't had any affect
        number_of_free_logs = _logger-> number_of_free_logs;

        // test the logger_clean function
        logger_clean(_logger);
        ASSERT_EQ(_logger->number_of_free_logs, number_of_free_logs);
    }
    /**
     * Test logger_clean() function provided that Offline Analyzer hasn't read
     * the logs yet but RT Analyzer did.
     * The expected result of this test is that none of the logs will be cleand.
     * - make sure that logger_clean() doesn't clean any logs
     *   if log->offline_analyzer_done = false and log->rt_analyzer_done = true
     */
    TEST_P(LogMgrUnitTest, CleanOfflineAnalyzerNotDone)
    {
        Byte placeholder = 'x';
        char read_res[1];
        int number_of_free_logs;

        // write full log
        for (unsigned long int i = 0; i < ((LOG_SIZE * _logger->number_of_allocated_logs)); i++) {
            ASSERT_EQ(0, logger_write(_logger, &placeholder, 1));
        }
        // read full log
        for (unsigned long int i = 0; i < (LOG_SIZE * _logger->number_of_allocated_logs); i++) {
            ASSERT_EQ(1, logger_read(_logger, (Byte*)read_res, 1));
        }

        // save this number to make sure that logger clean function
        // didn't had any affect
        number_of_free_logs = _logger-> number_of_free_logs;

        // test the logger_clean function
        logger_clean(_logger);
        ASSERT_EQ(_logger->number_of_free_logs, number_of_free_logs);
    }
    /**
     * Test cleaning half of the logs.
     * Write all the logs in the logger pool and read half of them to check
     * that logger_clean() function cleans only the part of the logs that have
     * been written and read by both RT analyzer and Offline analyzer.
     * - make sure that exectly half of the logs are cleaned
     *   the rest of the logs should not be cleaned
     */
    TEST_P(LogMgrUnitTest, CleanHalfOfTheLogger)
    {
        Byte placeholder = 'x';
        char read_res[1];
        Log* log;

        // write full log
        for (unsigned long int i = 0; i < ((LOG_SIZE * _logger->number_of_allocated_logs)); i++) {
            ASSERT_EQ(0, logger_write(_logger, &placeholder, 1));
        }

        // read half log
        for (unsigned long int i = 0; i < ((LOG_SIZE * _logger->number_of_allocated_logs)/2); i++) {
            ASSERT_EQ(1, logger_read(_logger, (Byte*)read_res, 1));
        }

        // mark half of the logs as being done analyzing by the offline analyzer
        log = _logger->dummy_log->next;
        for(uint32_t i = 0; (i < (_logger->number_of_allocated_logs/2)) && (log != _logger->dummy_log); i++) {
            log->offline_analyzer_done = true;
            log = log->next;
        }

        // test number of free logs before executing logger_clean
        ASSERT_EQ(_logger->number_of_free_logs, 1);

        // test the logger_clean() function
        logger_clean(_logger);
        ASSERT_EQ(_logger->number_of_free_logs, ((_logger->number_of_allocated_logs/2)+1));
    }
    /**
     * Test that if no minimum time of LOG_MAX_UNUSED_TIME_SECONDS has
     * elapsed between reading/writing the log and trying to reduce
     * it's size then the log will not be reduced.
     *  - make sure logger_reduce_size() function doesn't reduce a log if
     *    current_time - log->last_used_timestamp
     *    is not greater then LOG_MAX_UNUSED_TIME_SECONDS
     */
    TEST_P(LogMgrUnitTest, ReduceLoggerWithNoSleep)
    {
        Byte placeholder = 'x';
        char read_res[1];
        int logger_size_before_reduce;

        // write full log
        for (unsigned long int i = 0; i < ((LOG_SIZE * _logger->number_of_allocated_logs)); i++) {
            ASSERT_EQ(0, logger_write(_logger, &placeholder, 1));
        }
        // read full log
        for (unsigned long int i = 0; i < (LOG_SIZE * _logger->number_of_allocated_logs); i++) {
            ASSERT_EQ(1, logger_read(_logger, (Byte*)read_res, 1));
        }

        // save current number of logs before reduction
        logger_size_before_reduce = _logger->current_number_of_logs;

        // try to reduce logger size without a sleep
        // logger_reduce_size should not reduce any logs
        // on this test
        logger_reduce_size(_logger);
        ASSERT_EQ(logger_size_before_reduce, _logger->current_number_of_logs);
    }
    /**
     * Test that if minimum time of LOG_MAX_UNUSED_TIME_SECONDS has
     * elapsed between reading/writing the log and trying to reduce
     * it's size then the log will be reduced.
     *  - make sure logger_reduce_size() function reduce a log if
     *    current_time - log->last_used_timestamp
     *    is greater then LOG_MAX_UNUSED_TIME_SECONDS
     *  - make sure the logger_clean function cleans the logs after
     *    logger_reduce_size() has done its work on the logger pool.
     */
    TEST_P(LogMgrUnitTest, CleanReduceOneLogWithSleep)
    {
        Byte placeholder = 'x';
        char read_res[1];
        int logger_size_before_reduce;
        Log* log;

        // write full log
        for (unsigned long int i = 0; i < ((LOG_SIZE * _logger->number_of_allocated_logs)); i++) {
            ASSERT_EQ(0, logger_write(_logger, &placeholder, 1));
        }
        // read full log
        for (unsigned long int i = 0; i < (LOG_SIZE * _logger->number_of_allocated_logs); i++) {
            ASSERT_EQ(1, logger_read(_logger, (Byte*)read_res, 1));
        }

        // sleep for LOG_MAX_UNUSED_TIME_SECONDS to test
        sleep(LOG_MAX_UNUSED_TIME_SECONDS+1);

        // mark all logs as being done analyzing by the offline analyzer
        log = _logger->dummy_log->next;
        while( log != _logger->dummy_log)
        {
            log->offline_analyzer_done = true;
            log = log->next;
        }

        // save current number of logs before reduction
        logger_size_before_reduce = _logger->current_number_of_logs;

        // try to reduce the size of the logger
        // there should have been allocated one extra log
        // check that it is reduced
        logger_reduce_size(_logger);
        ASSERT_EQ(logger_size_before_reduce-1, _logger->current_number_of_logs);

        // test number of free logs before executing logger_clean
        ASSERT_EQ(_logger->number_of_free_logs, 1);

        // test the logger_clean() function.
        // number of free logs after the clean should equal number of allocated logs
        // because all logs have been written to and read by both the RT and Offline
        // analyzers
        logger_clean(_logger);
        ASSERT_EQ(_logger->number_of_free_logs, _logger->number_of_allocated_logs);
    }
    /**
     * Test the same as CleanReduceOneLogWithSleep but
     * make sure the logger_reduce_size() cat reduce multiple logs.
     */
    TEST_P(LogMgrUnitTest, CleanReduceMultipuleLogsWithSleep)
    {
        Byte placeholder = 'x';
        char read_res[1];
        int logger_size_before_reduce;
        Log* log;

        // write full log
        for (unsigned long int i = 0; i < ((2 * LOG_SIZE * _logger->number_of_allocated_logs)); i++) {
            ASSERT_EQ(0, logger_write(_logger, &placeholder, 1));
        }
        // read full log
        for (unsigned long int i = 0; i < (2 * LOG_SIZE * _logger->number_of_allocated_logs); i++) {
            ASSERT_EQ(1, logger_read(_logger, (Byte*)read_res, 1));
        }

        // sleep for LOG_MAX_UNUSED_TIME_SECONDS to test
        sleep(LOG_MAX_UNUSED_TIME_SECONDS+1);

        // mark all logs as being done analyzing by the offline analyzer
        log = _logger->dummy_log->next;
        while( log != _logger->dummy_log)
        {
            log->offline_analyzer_done = true;
            log = log->next;
        }

        // save current number of logs before reduction
        logger_size_before_reduce = _logger->current_number_of_logs;

        // try to reduce the size of the logger
        // there should have been allocated 2*number_of_originally_allocated extra logs
        // check that they are all reduced
        logger_reduce_size(_logger);
        ASSERT_EQ(((logger_size_before_reduce-1)/2), _logger->current_number_of_logs);

        // test number of free logs before executing logger_clean
        ASSERT_EQ(_logger->number_of_free_logs, 1);

        // test the logger_clean() function.
        // number of free logs after the clean should equal number of allocated logs
        // because all logs have been written to and read by both the RT and Offline
        // analyzers
        logger_clean(_logger);
        ASSERT_EQ(_logger->number_of_free_logs, _logger->number_of_allocated_logs);
    }
    /**
     * Test filling up the log and then writting more data:
     * - make sure filling up the buffer returns zero
     */
    TEST_P(LogMgrUnitTest, FullBuffer) {
        Byte offset[4];
        Byte placeholder = 'y';
        // write and read the offset
        ASSERT_EQ(0, logger_write(_logger, offset, sizeof(offset)));
        ASSERT_EQ(sizeof(offset), logger_read(_logger, offset, sizeof(offset)));
        // almost fill the buffer
        for (unsigned int i = 0; i < ((LOG_SIZE * _logger->number_of_allocated_logs)); i++)
            ASSERT_EQ(0, logger_write(_logger, &placeholder, 1));
        // try to fill the buffer completely (one slot empty means a full buffer)
        ASSERT_EQ(0, logger_write(_logger, &placeholder, 1));
    }
    /**
     * Test writting and reading when all the log's
     * in logger_pool are full:
     * - make sure writing on full logger_pool works
     * - make sure reading on full logger_pool works
     */
    TEST_P(LogMgrUnitTest, FullPoolStringWriteRead) {
        // fill the buffer
        Byte placeholder = 'x';
        for (unsigned long int i = 0; i < ((LOG_SIZE * _logger->number_of_allocated_logs)); i++) {
            ASSERT_EQ(0, logger_write(_logger, &placeholder, 1));
        }
        // write test string
        ASSERT_EQ(0, logger_write(_logger, (Byte*) TEST_STRING, sizeof(TEST_STRING)));
        // read full log
        for (unsigned long int i = 0; i < (LOG_SIZE * _logger->number_of_allocated_logs); i++) {
            ASSERT_EQ(1, logger_read(_logger, &placeholder, 1));
        }
        // read the written test string
        char res[2 * sizeof(TEST_STRING)];
        ASSERT_EQ(sizeof(TEST_STRING), logger_read(_logger, (Byte*) res, sizeof(TEST_STRING)));
        ASSERT_STREQ(res, TEST_STRING);
    }
    /**
     * Test reading back a string which was written on the boundary of the buffer:
     * - make sure writing at buffer boundary works
     * - make sure reading from buffer boundary works
     */
    TEST_P(LogMgrUnitTest, WrapStringWriteRead) {
        // fill the buffer
        Byte placeholder = 'x';
        for (unsigned int i = 0; i < LOG_SIZE - (sizeof(TEST_STRING)); i++) {
            ASSERT_EQ(0, logger_write(_logger, &placeholder, 1));
        }
        // increment the reader
        for (unsigned int i = 0; i < LOG_SIZE - (sizeof(TEST_STRING)); i++) {
            ASSERT_EQ(1, logger_read(_logger, &placeholder, 1));
        }
        // write the string and read it back
        ASSERT_EQ(0, logger_write(_logger, (Byte*) TEST_STRING, sizeof(TEST_STRING)));
        char res[2 * sizeof(TEST_STRING)];
        ASSERT_EQ(sizeof(TEST_STRING), logger_read(_logger, (Byte*) res, sizeof(TEST_STRING)));
        ASSERT_STREQ(res, TEST_STRING);
    }
    /**
     * Test reading back a string which was half written of one log and half on another:
     * - make sure writing and crossing the buffer boundary works
     */
    TEST_P(LogMgrUnitTest, CrossBoundryStringWriteRead) {
        Byte placeholder = 'x';

        // fill the buffer and leave enough place for half of the test string
        for (unsigned int i = 0; i < LOG_SIZE - (sizeof(TEST_STRING)/2); i++) {
            ASSERT_EQ(0, logger_write(_logger, &placeholder, 1));
        }
        // write the test string
        ASSERT_EQ(0, logger_write(_logger, (Byte*) TEST_STRING, sizeof(TEST_STRING)));

        // increment the reader until the start fo the test string
        for (unsigned int i = 0; i < LOG_SIZE - (sizeof(TEST_STRING)/2); i++) {
            ASSERT_EQ(1, logger_read(_logger, &placeholder, 1));
        }
        // read the string back
        char res[sizeof(TEST_STRING)];
        ASSERT_EQ(sizeof(TEST_STRING), logger_read(_logger, (Byte*) res, sizeof(TEST_STRING)));
        ASSERT_STREQ(res, TEST_STRING);
    }
    /**
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
    /**
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
    /**
     * Test writing and reading a physical cell read log
     */
    TEST_P(LogMgrUnitTest, PhysicalCellRead) {
        struct timeval logging_parser_tv;
        TIME_MICROSEC(start);
        PhysicalCellReadLog log = {
            .channel = 3,
            .block = 80,
            .page = 123,
            .metadata = {start, start+1}
        };
        LOG_PHYSICAL_CELL_READ(_logger, log);
        ASSERT_EQ(PHYSICAL_CELL_READ_LOG_UID, next_log_type(_logger));
        PhysicalCellReadLog res = NEXT_PHYSICAL_CELL_READ_LOG(_logger);
        ASSERT_EQ(log.channel, res.channel);
        ASSERT_EQ(log.block, res.block);
        ASSERT_EQ(log.page, res.page);
    }
    /**
     * Test writing and reading a physical cell program log
     */
    TEST_P(LogMgrUnitTest, PhysicalCellProgram) {
        struct timeval logging_parser_tv;
        TIME_MICROSEC(start);
        PhysicalCellProgramLog log = {
            .channel = 15,
            .block = 63,
            .page = 50,
            .metadata = {start, start+1}
        };
        LOG_PHYSICAL_CELL_PROGRAM(_logger, log);
        ASSERT_EQ(PHYSICAL_CELL_PROGRAM_LOG_UID, next_log_type(_logger));
        PhysicalCellProgramLog res = NEXT_PHYSICAL_CELL_PROGRAM_LOG(_logger);
        ASSERT_EQ(log.channel, res.channel);
        ASSERT_EQ(log.block, res.block);
        ASSERT_EQ(log.page, res.page);
    }
    /**
     * Test writing and reading a logical cell program log
     */
    TEST_P(LogMgrUnitTest, LogicalCellProgram) {
        struct timeval logging_parser_tv;
        TIME_MICROSEC(start);
        LogicalCellProgramLog log = {
            .channel = 2,
            .block = 260,
            .page = 3,
            .metadata = {start, start+1}
        };
        LOG_LOGICAL_CELL_PROGRAM(_logger, log);
        ASSERT_EQ(LOGICAL_CELL_PROGRAM_LOG_UID, next_log_type(_logger));
        LogicalCellProgramLog res = NEXT_LOGICAL_CELL_PROGRAM_LOG(_logger);
        ASSERT_EQ(log.channel, res.channel);
        ASSERT_EQ(log.block, res.block);
        ASSERT_EQ(log.page, res.page);
    }
    /**
     * Test writing and reading a garbage collection log
     */
    TEST_P(LogMgrUnitTest, GarbageCollection) {
        GarbageCollectionLog log = { };
        LOG_GARBAGE_COLLECTION(_logger, log);
        ASSERT_EQ(GARBAGE_COLLECTION_LOG_UID, next_log_type(_logger));
        // test that NEXT_GARBAGE_COLLECTION_LOG actually does nothing,
        // due to the fact that the struct is empty
        Byte placeholder;
        ASSERT_EQ(0, logger_read(_logger, &placeholder, 0));
        NEXT_GARBAGE_COLLECTION_LOG(_logger);
        ASSERT_EQ(0, logger_read(_logger, &placeholder, 0));
    }
    /**
     * Test writing and reading a register read log
     */
    TEST_P(LogMgrUnitTest, RegisterReadLog) {
        struct timeval logging_parser_tv;
        TIME_MICROSEC(start);
        RegisterReadLog log = {
            .channel = 10,
            .die = 15,
            .reg = 37,
            .metadata = {start, start+1}
        };
        LOG_REGISTER_READ(_logger, log);
        ASSERT_EQ(REGISTER_READ_LOG_UID, next_log_type(_logger));
        RegisterReadLog res = NEXT_REGISTER_READ_LOG(_logger);
        ASSERT_EQ(log.channel, res.channel);
        ASSERT_EQ(log.die, res.die);
        ASSERT_EQ(log.reg, res.reg);
    }
    /**
     * Test writing and reading a register write log
     */
    TEST_P(LogMgrUnitTest, RegisterWriteLog) {
        struct timeval logging_parser_tv;
        TIME_MICROSEC(start);
        RegisterWriteLog log = {
            .channel = 87013,
            .die = 225034,
            .reg = 4,
            .metadata = {start, start+1}
        };
        LOG_REGISTER_WRITE(_logger, log);
        ASSERT_EQ(REGISTER_WRITE_LOG_UID, next_log_type(_logger));
        RegisterWriteLog res = NEXT_REGISTER_WRITE_LOG(_logger);
        ASSERT_EQ(log.channel, res.channel);
        ASSERT_EQ(log.die, res.die);
        ASSERT_EQ(log.reg, res.reg);
    }
    /**
     * Test writing and reading a block erase log
     */
    TEST_P(LogMgrUnitTest, BlockEraseLog) {
        struct timeval logging_parser_tv;
        TIME_MICROSEC(start);
        BlockEraseLog log = {
            .channel = 6,
            .die = 352,
            .block = 947,
            .metadata = {start, start+1}
        };
        LOG_BLOCK_ERASE(_logger, log);
        ASSERT_EQ(BLOCK_ERASE_LOG_UID, next_log_type(_logger));
        BlockEraseLog res = NEXT_BLOCK_ERASE_LOG(_logger);
        ASSERT_EQ(log.channel, res.channel);
        ASSERT_EQ(log.die, res.die);
        ASSERT_EQ(log.block, res.block);
    }
    /**
     * Test writing and reading a channel switch to read log
     */
    TEST_P(LogMgrUnitTest, ChannelSwitchToReadLog) {
        struct timeval logging_parser_tv;
        TIME_MICROSEC(start);
        ChannelSwitchToReadLog log = {
            .channel = 73,
            .metadata = {start, start+1}
        };
        LOG_CHANNEL_SWITCH_TO_READ(_logger, log);
        ASSERT_EQ(CHANNEL_SWITCH_TO_READ_LOG_UID, next_log_type(_logger));
        ChannelSwitchToReadLog res = NEXT_CHANNEL_SWITCH_TO_READ_LOG(_logger);
        ASSERT_EQ(log.channel, res.channel);
    }
    /**
     * Test writing and reading a channel switch to read write
     */
    TEST_P(LogMgrUnitTest, ChannelSwitchToWriteLog) {
        struct timeval logging_parser_tv;
        TIME_MICROSEC(start);
        ChannelSwitchToWriteLog log = {
            .channel = 3,
            .metadata = {start, start+1}
        };
        LOG_CHANNEL_SWITCH_TO_WRITE(_logger, log);
        ASSERT_EQ(CHANNEL_SWITCH_TO_WRITE_LOG_UID, next_log_type(_logger));
        ChannelSwitchToWriteLog res = NEXT_CHANNEL_SWITCH_TO_WRITE_LOG(_logger);
        ASSERT_EQ(log.channel, res.channel);
    }
    /* Real Time Analyzer Tests */
    /**
     * Do a simple test of the real time log analyzer
     */
    TEST_P(LogMgrUnitTest, BasicRTAnalyzer) {
        RTLogAnalyzer* analyzer = rt_log_analyzer_init(_logger, 0);
        rt_log_stats_init();
        rt_subscriber::subscribe(analyzer);
        if (g_server_mode)
            rt_log_analyzer_subscribe(analyzer, (MonitorHook) log_server_update, NULL);
        rt_subscriber::write();
        rt_subscriber::read();
        rt_log_analyzer_free(analyzer, 0);
        rt_log_stats_free();
    }

    /* offline Analyzer Tests */
    /**
     * Do a simple sanity test of the offline time log analyzer
     * make sure :
     *      - we are able to create offline logger analyzer thread
     *      - offline logger analyzer exits on request
     *      - offline logger analyzer cleans it's resources
     */
    TEST_P(LogMgrUnitTest, BasicOfflineAnalyzer) {
        Byte placeholder = 'x';
        char read_res[1];
        pthread_t offline_log_analyzer_thread;
        OfflineLogAnalyzer* analyzer = offline_log_analyzer_init(_logger);

        // create thread for the offline log analyzer
        pthread_create(&offline_log_analyzer_thread, NULL,
                offline_log_analyzer_run, analyzer);

        // write full log
        for (unsigned long int i = 0; i < ((LOG_SIZE * _logger->number_of_allocated_logs)); i++) {
            ASSERT_EQ(0, logger_write(_logger, &placeholder, 1));
        }
        // read full log
        for (unsigned long int i = 0; i < (LOG_SIZE * _logger->number_of_allocated_logs); i++) {
            ASSERT_EQ(1, logger_read(_logger, (Byte*)read_res, 1));
        }

        // free && clean up's of the offline log analyzer
        analyzer->exit_loop_flag = 1;
        pthread_join(offline_log_analyzer_thread, NULL);
        offline_log_analyzer_free(analyzer);
    }
    /* Log Manager Tests */
    /**
     * Do a simple test of the log manager
     */
    TEST_P(LogMgrUnitTest, BasicLogManager) {
        LogManager* manager = log_manager_init();
        manager_subscriber::init(manager);
        if (g_server_mode)
            log_manager_subscribe(manager, (MonitorHook) log_server_update, NULL);
        manager_subscriber::run();
        manager_subscriber::free();
        log_manager_free(manager);
    }
} //namespace
