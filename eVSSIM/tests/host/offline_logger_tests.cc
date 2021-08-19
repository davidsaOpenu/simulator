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

#include "base_emulator_tests.h"
#include "logging_backend.h"
#include <stdlib.h>
#include <dirent.h>
#include <time.h>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <json.h>
#include <unordered_map>

// For logger_writer
extern elk_logger_writer elk_logger_writer_obj;

#define MONITOR_SYNC_DELAY_USEC 150000
#define DELAY_THRESHOLD 0

void MONITOR_SYNC_DELAY(int expected_duration) {
    usleep(expected_duration + MONITOR_SYNC_DELAY_USEC);
}

int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

using namespace std;

namespace {

    class OfflineLoggerTest : public BaseEmulatorTests {
    public:
        static void SetUpTestCase();
        static void TearDownTestCase();
        virtual void SetUp();
        virtual void TearDown();
    };

    std::vector<std::tuple<size_t,size_t, int, int> > GetParams() {
        std::vector<std::tuple<size_t,size_t, int, int> > list;
        const int constFlashNum = DEFAULT_FLASH_NB;
        unsigned int i = 0;
        for (int j = 0; j < 16; j++)
            for (int k = 0; k < 3; k++)
                list.push_back(std::make_tuple(parameters::Allsizemb[i], constFlashNum, j, k ));
        return list;
    }

    INSTANTIATE_TEST_CASE_P(OfflineLogger, OfflineLoggerTest,
        testing::ValuesIn(GetParams()));

    #define LOG_FILE_PREFIX "elk_log_file-"
    #define LOG_FILE_PREFIX_LEN sizeof(LOG_FILE_PREFIX)

    enum {MODE_R, MODE_W, MODE_RW };

    std::vector<std::string> get_all_log_files(const char *logs_path) {
        std::vector<std::string> ret;
        DIR *dir;
        struct dirent *ent;

        if ((dir = opendir (logs_path)) != NULL) {
            while ((ent = readdir (dir)) != NULL) {
                if (!strncmp(ent->d_name, LOG_FILE_PREFIX, LOG_FILE_PREFIX_LEN-1)) {
                    ret.push_back(string(logs_path) + string(ent->d_name));
                }
            }
            closedir (dir);
        }
        return ret;
    }

    time_t sort_log_files_by_dates_fn_helper(std::string a) {
        const char *time_fmt = "%Y-%m-%d_%H:%M:%S";

        size_t a_time_end = a.find(".log");
        std::string a_time_str = a.substr(LOG_FILE_PREFIX_LEN, a_time_end);

        struct tm tm_a;
        strptime(a_time_str.c_str(), time_fmt, &tm_a);

        return mktime(&tm_a);
    }

    bool sort_log_files_by_dates_fn(std::string a, std::string b) {
        time_t t_a = sort_log_files_by_dates_fn_helper(a);
        time_t t_b = sort_log_files_by_dates_fn_helper(b);

        double diff = difftime(t_a, t_b);

        return diff;
    }

    std::unordered_map<std::string, int> get_new_stats() {
        std::unordered_map<std::string, int> stats = {
            {"LogicalCellProgramLog", 0},
            {"PhysicalCellProgramLog", 0},
            {"PhysicalCellReadLog", 0},
            {"RegisterReadLog", 0},
            {"RegisterWriteLog", 0},        
            {"ChannelSwitchToReadLog", 0},
            {"ChannelSwitchToWriteLog", 0}
        };
        return stats;
    }

    std::unordered_map<std::string, int> predict_test_stats(int num_blocks, int mode, int CONST_PAGES_PER_BLOCK) {
        std::unordered_map<std::string, int> stats = get_new_stats();

        switch(mode) {
            case MODE_R:
                stats["PhysicalCellReadLog"] = num_blocks * CONST_PAGES_PER_BLOCK;
                stats["RegisterReadLog"] = num_blocks * CONST_PAGES_PER_BLOCK;
                break;
            case MODE_W:
                stats["RegisterWriteLog"] = num_blocks * CONST_PAGES_PER_BLOCK;
                stats["LogicalCellProgramLog"] = num_blocks * CONST_PAGES_PER_BLOCK;
                stats["PhysicalCellProgramLog"] = num_blocks * CONST_PAGES_PER_BLOCK;
                break;
            case MODE_RW:
                stats["RegisterWriteLog"] = num_blocks * CONST_PAGES_PER_BLOCK;
                stats["RegisterReadLog"] = num_blocks * CONST_PAGES_PER_BLOCK;
                stats["LogicalCellProgramLog"] = num_blocks * CONST_PAGES_PER_BLOCK;
                stats["PhysicalCellProgramLog"] = num_blocks * CONST_PAGES_PER_BLOCK;
                stats["PhysicalCellReadLog"] = num_blocks * CONST_PAGES_PER_BLOCK;
                stats["ChannelSwitchToReadLog"] = num_blocks * CONST_PAGES_PER_BLOCK-1; // Minus 1 as first read doesn't cause channel switch
                stats["ChannelSwitchToWriteLog"] = num_blocks * CONST_PAGES_PER_BLOCK;
                break;
        }
        return stats;
    }

    std::unordered_map<std::string, int> get_test_stats(std::vector<std::string> log_files) {
        std::unordered_map<std::string, int> stats = get_new_stats();

        for(auto &log_file : log_files) {
            ifstream log_file_is(log_file);
            std::string line;

            while (getline(log_file_is, line)) {
                json_object *j_obj, *type_obj; 

                j_obj = json_tokener_parse(line.c_str());
                json_object_object_get_ex(j_obj, "type", &type_obj);

                std::string type = std::string(json_object_get_string(type_obj));
                stats[type]++;
            }
        }

        return stats;
    }

    int check_offline_logger_test_results(const char *logs_path, int num_blocks, int mode, int CONST_PAGES_PER_BLOCK) {
        // First get all the log files
        std::vector<std::string> log_files = get_all_log_files(logs_path);
        if (log_files.empty())
            return -1;

        // Now sort the log files by their time
        std::sort(log_files.begin(), log_files.end(), sort_log_files_by_dates_fn);

        // Read all the lines of the log files, json parse them, and aggregate them to the statistics object
        std::unordered_map<std::string, int> cur_stats = get_test_stats(log_files);

        // Get the expected statistics based on num_blocks and mode
        std::unordered_map<std::string, int> predicted_stats = predict_test_stats(num_blocks, mode, CONST_PAGES_PER_BLOCK);

        // Compare the two
        for (auto &key : cur_stats) {
            cout << key.first << " cur: " << cur_stats[key.first] << " predicted: " << predicted_stats[key.first] << endl;
            if (cur_stats[key.first] != predicted_stats[key.first])
                return -1;
        }
        
        return 0;
    }

    /**
     * testing of the offline analyzer mechanism related to the json serialization
     */
    TEST_P(OfflineLoggerTest, LoggerWriterPageWriteTest) {
        std::tuple<size_t,size_t, int, int> params = GetParam();
        //int flash_nb = std::get<1>(params);
        int pow = std::get<2>(params);
        int mode = std::get<3>(params);
        int num_blocks = 1;

        for (int i = 0; i < pow; i++, num_blocks *= 2);

        printf("[+] Running test for pow = %d, mode = %d\n", pow, mode);

        /**
         * Test outline:
         *  for each pow of 2 from 0 to X:
         *      for each mode r/w/rw:
         *          run test for the mode
         *          predict the sum of all read/writes/etc..
         *          parse the json file and get the statistics
         *          check wether the statistics are the same
         *          for restarting  the environment:
         *              logger_writer_free
         *              logger_writer_init
         */

        // single write page delay
        int expected_write_duration = CHANNEL_SWITCH_DELAY_W + (REG_WRITE_DELAY + CELL_PROGRAM_DELAY);
        int expected_read_duration = CHANNEL_SWITCH_DELAY_R + (REG_READ_DELAY + CELL_READ_DELAY);
        uint64_t expected_duration = 0;

        // In case of r/w mode: read / write num_blocks
        for (int block_nb = 0; block_nb < num_blocks; block_nb++) {
            for (size_t i = 0; i < CONST_PAGES_PER_BLOCK; i++) {
                switch (mode) {
                    case MODE_R:
                        SSD_PAGE_READ(0, block_nb, i, 0, READ);
                        expected_duration += expected_read_duration;
                        break;
                    case MODE_W:
                        SSD_PAGE_WRITE(0, block_nb, i, 0, WRITE);
                        expected_duration += expected_write_duration;
                        break;
                    case MODE_RW:
                        SSD_PAGE_READ(0, block_nb, i, 0, READ);
                        SSD_PAGE_WRITE(0, block_nb, i, 0, WRITE);
                        expected_duration += expected_read_duration + expected_write_duration;
                        break;
                }
            }
        }

/*
        // Make sure that the offline logger analyzed everything
        Logger_Pool* logger = GET_LOGGER(flash_nb);
        while (1) {
            printf("ITER\n");
            Log *log = logger->dummy_log->next;
            bool not_all_done = false;

            // edge case where still nothing was written
            if (log == logger->dummy_log) {
                usleep(100000);
                continue;
            }

            do {
                if (log->head != log->offline_tail) {
                    printf("not_all_done %ld\n", log->head - log->offline_tail);
                    not_all_done = true;
                    break;
                }
                log = log->next;
            } while (log == logger->dummy_log);

            if (!not_all_done)
                break;
            usleep(1000000);
        }*/
        MONITOR_SYNC_DELAY(expected_duration);
        printf("Done waiting!\n");

        // Check that the test passed OK
        ASSERT_EQ(0, check_offline_logger_test_results("/code/logs/", num_blocks, mode, CONST_PAGES_PER_BLOCK));
    }

    void OfflineLoggerTest::SetUpTestCase() {
        PINFO("## SetUpTestCase\n");
        BaseEmulatorTests::SetUpEnv(8, 4);
        FTL_TERM();
        g_init = 0;
        elk_logger_writer_free();
    }

    void OfflineLoggerTest::TearDownTestCase() {
        /** The  following call to elk_logger_init() will delete the log files and hence is not good for the case
         * of using thee logs after the tests (This is the case for being used by the ELK stack tests), so for now it
         * is commented out. For now this is ok becasue the test exits after one test case. */
        /*elk_logger_writer_init();
        FTL_INIT();
        TearDownEnv();*/
    }

    void OfflineLoggerTest::SetUp() {
        PINFO("## SetUp\n");
        FTL_INIT();
        elk_logger_writer_init();
    }

    void OfflineLoggerTest::TearDown() {
        PINFO("## TearDown\n");
        FTL_TERM();
        g_init = 0;
        elk_logger_writer_free();
    }


} //namespace
