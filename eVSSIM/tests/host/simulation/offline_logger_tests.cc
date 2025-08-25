/*
 * Copyright 2023 The Open University of Israel
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
extern int auto_delete;

#define PAGE_SIZE 1024
#define PAGE_NB 10
#define SECTOR_SIZE 1
#define MAX_POW 6
#define MAX_MODE 2
#define POW_START 1
#define SYSCALL_DELAY 10000

using namespace std;

namespace offline_logger_test{



void flipAuto();

    class OfflineLoggerTest : public BaseTest {
        public:
            virtual void SetUp(){
                BaseTest::SetUp();
                INIT_LOG_MANAGER();
            }
                
            virtual void TearDown(){
                BaseTest::TearDown();
                TERM_LOG_MANAGER();
            }
    };
    
    
    /**
     * disables/enables the auto deletion of log files sent by filebeat and deletes all remaining logs 
     */
    void flipAuto(){
        auto_delete = !auto_delete;
        usleep(SYSCALL_DELAY);
        int retval = system(LOG_FILE_REMOVAL_COMMAND);
        if(WIFEXITED(retval)){
            int exitStatus = WEXITSTATUS(retval);
            if(exitStatus!=0){
                printf("ERROR could not execute system call with exit status %d\n", exitStatus);
            }
        }
    }
    
    std::vector<SSDConf*> GetTestParams() {
        std::vector<SSDConf*> ssd_configs;

        for(unsigned int i = POW_START; i <= MAX_POW;i++){
            size_t block_nb = pow(2,i);
            ssd_configs.push_back(new SSDConf(PAGE_SIZE, PAGE_NB, SECTOR_SIZE, DEFAULT_FLASH_NB, block_nb, DEFAULT_FLASH_NB, block_nb * DEFAULT_FLASH_NB, 0));
        }

        return ssd_configs;
    }

    INSTANTIATE_TEST_CASE_P(DiskSize, OfflineLoggerTest, ::testing::ValuesIn(GetTestParams()));


    #define LOG_FILE_PREFIX "elk_log_file-"
    #define LOG_FILE_PREFIX_LEN sizeof(LOG_FILE_PREFIX)

    enum {MODE_R, MODE_W, MODE_RW };
    
    /**
     * returns a vector of the paths to all the logs 
     * @param logs_path the path to the logs
     */
    std::vector<std::string> get_all_log_files(const char *logs_path) {
        std::vector<std::string> ret = {};
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
        if(ret.size() == 0){
            printf("error: no files found \n");
        }
        
        return ret;
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

    std::unordered_map<std::string, int> predict_test_stats(int mode) {
        std::unordered_map<std::string, int> stats = get_new_stats();
        switch(mode) {
            case MODE_R:
                stats["PhysicalCellReadLog"] = PAGES_IN_SSD;
                stats["RegisterReadLog"] = PAGES_IN_SSD;
                stats["ChannelSwitchToReadLog"] = FLASH_NB;
                break;
            case MODE_W:
                stats["RegisterWriteLog"] = PAGES_IN_SSD;
                stats["LogicalCellProgramLog"] = PAGES_IN_SSD;
                stats["PhysicalCellProgramLog"] = PAGES_IN_SSD;
                stats["ChannelSwitchToWriteLog"] = FLASH_NB;
                break;
            case MODE_RW:
                stats["RegisterWriteLog"] = PAGES_IN_SSD;
                stats["RegisterReadLog"] = PAGES_IN_SSD;
                stats["LogicalCellProgramLog"] = PAGES_IN_SSD;
                stats["PhysicalCellProgramLog"] = PAGES_IN_SSD;
                stats["PhysicalCellReadLog"] = PAGES_IN_SSD;
                stats["ChannelSwitchToReadLog"] = PAGES_IN_SSD;
                stats["ChannelSwitchToWriteLog"] = PAGES_IN_SSD;
                break;
        }
        return stats;
    }

    std::unordered_map<std::string, int> get_test_stats(std::vector<std::string> log_files) {
        std::unordered_map<std::string, int> stats = get_new_stats();
        json_object *j_obj, *type_obj; 
        for(auto &log_file : log_files) {
            ifstream log_file_is(log_file);
            std::string line;

            while (getline(log_file_is, line)) {

                j_obj = json_tokener_parse(line.c_str());
                json_object_object_get_ex(j_obj, "type", &type_obj);
                
                std::string type = std::string(json_object_get_string(type_obj));
                stats[type]++;
                
                //json_object_put(type_obj);
                json_object_put(j_obj);
            }
        }
        
        
        return stats;
    }

    /**
     * checks if the content of the log files are as predicted
     * @param logs_path the path to the logs
     * @param mode the type of mode R/W/RW
     */
    int check_offline_logger_test_results(const char *logs_path, int mode) {
        // First get all the log files
        std::vector<std::string> log_files = get_all_log_files(logs_path);
        if (log_files.empty()){
            printf("ERROR no logs\n");
            return -1;
        }

        // Read all the lines of the log files, json parse them, and aggregate them to the statistics object
        std::unordered_map<std::string, int> cur_stats = get_test_stats(log_files);

        // Get the expected statistics based on num_blocks and mode
        std::unordered_map<std::string, int> predicted_stats = predict_test_stats(mode);

        // Compare the two
        for (auto &key : cur_stats) {
            cout << key.first << " cur: " << cur_stats[key.first] << " predicted: " << predicted_stats[key.first] << endl;
            if (cur_stats[key.first] != predicted_stats[key.first]){
                return -1;
            }
                
        }
        
        return 0;
    }

    /**
     * testing of the offline analyzer mechanism related to the json serialization
     */
    void readOrWrite(int mode){
        
        int expected_write_duration = (CHANNEL_SWITCH_DELAY_W + REG_WRITE_DELAY + CELL_PROGRAM_DELAY) * PAGES_IN_SSD;
        int expected_read_duration = (CHANNEL_SWITCH_DELAY_R + REG_READ_DELAY + CELL_READ_DELAY) * PAGES_IN_SSD;

        // In case of r/w mode: read / write num_blocks

        for (uint64_t i = 0; i < PAGES_IN_SSD; i++) {
            switch (mode) {
                case MODE_R:
                    SSD_PAGE_READ(CALC_FLASH(i), CALC_BLOCK(i), i, 0, READ);
                    break;
                case MODE_W:
                    SSD_PAGE_WRITE(CALC_FLASH(i), CALC_BLOCK(i), i, 0, WRITE);
                    break;
                case MODE_RW:
                    SSD_PAGE_READ(CALC_FLASH(i), CALC_BLOCK(i), i, 0, READ);
                    SSD_PAGE_WRITE(CALC_FLASH(i), CALC_BLOCK(i), i, 0, WRITE);
                    break;
            }
        }
        
        switch (mode) {
            case MODE_R:
                  MONITOR_SYNC_DELAY(2*expected_read_duration);
                break;
            case MODE_W:
                MONITOR_SYNC_DELAY(expected_write_duration);
                break;
            case MODE_RW:
                MONITOR_SYNC_DELAY(expected_write_duration+expected_read_duration);
                break;
            default:
                printf("unknown mode \n");
        }

        printf("Done waiting!\n");
        
        // Check that the test passed OK

        int result = check_offline_logger_test_results("/code/logs/", mode);
        ASSERT_EQ(0, result);
       
    }

        
    /**
     * reads 2^n blocks from ssd and then checks if the logs were written to the log files correctly 
     */
    TEST_P(OfflineLoggerTest, LoggerWriterPageRead) {
        SSDConf* ssd_config = base_test_get_ssd_config();

        if(ssd_config->get_block_nb()==pow(2,POW_START)){
            flipAuto();
        }
        else{
            printf("[+] Running test for blocks = %lu, mode = %d\n", ssd_config->get_block_nb(), MODE_R);
            readOrWrite(MODE_R);
        }


    }
    
    /**
     * writes 2^n blocks from ssd and then checks if the logs were written to the log files correctly 
     */
    TEST_P(OfflineLoggerTest, LoggerWriterPageWrite) {
        SSDConf* ssd_config = base_test_get_ssd_config();

        printf("[+] Running test for blocks = %lu, mode = %d\n", ssd_config->get_block_nb(), MODE_W);

        readOrWrite(MODE_W);
    
    }
    
    /**
     * reads and then writes 2^n blocks from ssd and then checks if the logs were written to the log files correctly 
     */
    TEST_P(OfflineLoggerTest, LoggerWriterPageReadWrite) {
        SSDConf* ssd_config = base_test_get_ssd_config();
        printf("[+] Running test for blocks = %lu, mode = %d\n", ssd_config->get_block_nb(), MODE_RW);

        readOrWrite(MODE_RW);
                
        if(ssd_config->get_block_nb()==pow(2,MAX_POW)){
            flipAuto();
        }
    }

} //namespace
