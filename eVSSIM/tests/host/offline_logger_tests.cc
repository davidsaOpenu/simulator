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
extern int auto_delete;

#define MAX_POW 1
#define MAX_MODE 2
#define POW_START 1

#define MONITOR_SYNC_DELAY_USEC 150000
#define DELAY_THRESHOLD 0

void delay(int expected_duration){
	usleep(MONITOR_SYNC_DELAY_USEC + expected_duration);
}

using namespace std;

namespace offline_logger_test{



void flipAuto();

    class OfflineLoggerTest : public BaseTest {
		public:
			//const size_t CONST_PAGE_SIZE_IN_BYTES = SSDConf(parameters::Allsizemb[0]).get_page_size();
			//const size_t CONST_PAGES_NB = SSDConf(parameters::Allsizemb[0]).get_page_nb();

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
    
    	
	void flipAuto(){
    	auto_delete = !auto_delete;
    	usleep(1000000);
    	int retval = system("rm -rf " ELK_LOGGER_WRITER_LOGS_PATH "*.log");
    	if(retval == -1){
    		printf("ERROR could not execute system call \n");
    	}
    }
    
	std::vector<SSDConf*> GetTestParams() {
        std::vector<SSDConf*> ssd_configs;

		for(unsigned int i = POW_START; i <= MAX_POW;i++){
			ssd_configs.push_back(new SSDConf(1028, 10, 1, DEFAULT_FLASH_NB,pow(2,i), DEFAULT_FLASH_NB));
		}

        return ssd_configs;
    }

    INSTANTIATE_TEST_CASE_P(DiskSize, OfflineLoggerTest, ::testing::ValuesIn(GetTestParams()));


    #define LOG_FILE_PREFIX "elk_log_file-"
    #define LOG_FILE_PREFIX_LEN sizeof(LOG_FILE_PREFIX)

    enum {MODE_R, MODE_W, MODE_RW };

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
                stats["ChannelSwitchToReadLog"] = FLASH_NB;
                stats["ChannelSwitchToWriteLog"] = 0;
                break;
        }
        return stats;
	}

    std::unordered_map<std::string, int> get_test_stats(std::vector<std::string> log_files) {
        std::unordered_map<std::string, int> stats = get_new_stats();
		json_object *j_obj, *type_obj; 
		//j_obj = json_object_new_object();
		//type_obj = json_object_new_object();
		
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

    int check_offline_logger_test_results(const char *logs_path, int mode) {
        // First get all the log files
        std::vector<std::string> log_files = get_all_log_files(logs_path);
        if (log_files.empty()){
            printf("ERROR no logs\n");
            return -1;
		}

        // Now sort the log files by their time
        //std::sort(log_files.begin(), log_files.end(), sort_log_files_by_dates_fn);

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

        for (int i = 0; i < PAGES_IN_SSD; i++) {
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
  				delay(2*expected_read_duration);
                break;
            case MODE_W:
			    delay(expected_write_duration);
                break;
            case MODE_RW:
				delay(expected_write_duration+expected_read_duration);
                break;
            default:
            	printf("unknown mode \n");
        }

        printf("Done waiting!\n");
		
        // Check that the test passed OK

        int result = check_offline_logger_test_results("/code/logs/", mode);
        ASSERT_EQ(0, result);
       
    }

	    
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
	
    TEST_P(OfflineLoggerTest, LoggerWriterPageWrite) {
    	SSDConf* ssd_config = base_test_get_ssd_config();


        printf("[+] Running test for blocks = %lu, mode = %d\n", ssd_config->get_block_nb(), MODE_W);

		readOrWrite(MODE_W);
    
    }
    
	TEST_P(OfflineLoggerTest, LoggerWriterPageReadWrite) {
    	SSDConf* ssd_config = base_test_get_ssd_config();


        printf("[+] Running test for blocks = %lu, mode = %d\n", ssd_config->get_block_nb(), MODE_RW);

		readOrWrite(MODE_RW);
		
		    	
    	if(ssd_config->get_block_nb()==pow(2,MAX_POW)){
    		flipAuto();
    	}
	}

} //namespace
