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
 
#include <cmath>
#include "base_emulator_tests.h"

extern RTLogStatistics *rt_log_stats;
extern LogServer log_server;

#define ERROR_THRESHHOLD(x) x*0.01

#define CALCULATEMBPS(s,t) ((double)s*SECOND_IN_USEC)/MEGABYTE_IN_BYTES/t;

#define MAX_POW 8
#define PAGE_SIZE 4096
#define PAGE_NB 10
#define SECTOR_SIZE 1

using namespace std;

namespace write_read_test{

    class WriteReadTest : public BaseTest {
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
    
    
    std::vector<SSDConf*> GetTestParams() {
        std::vector<SSDConf*> ssd_configs;

        for(unsigned int i = 1; i <= MAX_POW;i++){
            ssd_configs.push_back(new SSDConf(PAGE_SIZE, PAGE_NB, SECTOR_SIZE, DEFAULT_FLASH_NB,pow(2,i), DEFAULT_FLASH_NB));
        }

        return ssd_configs;
    }
    
    /**
     * this test fills the entire disk in iterations starting from a disk size of 2 pages and continues until 2 ^ MAX_POW
     * the test never writes on a used page so that it wont incure garbage collection and keep the test deterministic
     * this test has 3 cases one in which it only writes, writes then reads and writes the entire disk then reads it
     */

    INSTANTIATE_TEST_CASE_P(DiskSize, WriteReadTest, ::testing::ValuesIn(GetTestParams()));
    
    /**
     * writes 2^n pages to the ssd,
     * checks that the stats on the monitor are correct
     */
    TEST_P(WriteReadTest, WRITEOnlyTest){
        SSDConf* ssd_config = base_test_get_ssd_config();
   
           //writes the whole ssd
        for(unsigned int p=0; p < ssd_config->get_pages(); p++){
             _FTL_WRITE_SECT(p * ssd_config->get_page_size(), 1);
        }     
        
        unsigned int time_per_action = REG_WRITE_DELAY + CELL_PROGRAM_DELAY+CHANNEL_SWITCH_DELAY_W;
        
        MONITOR_SYNC_DELAY(ssd_config->get_pages()*(time_per_action));
        
        double write_speed = CALCULATEMBPS(ssd_config->get_page_size(),time_per_action);
        
        
        //checks that log_server.stats (the stats on the monitor) are accurate
        ASSERT_NEAR(write_speed, log_server.stats.write_speed, ERROR_THRESHHOLD(write_speed));
        ASSERT_EQ(0, log_server.stats.read_speed);
        ASSERT_EQ(0, log_server.stats.read_count);
        ASSERT_EQ(ssd_config->get_pages(), log_server.stats.write_count);
        ASSERT_EQ(1, log_server.stats.write_amplification);
        ASSERT_EQ(1.0,log_server.stats.utilization);
        
    }
    
    /**
     * writes 2^n pages to the ssd and then reads them,
     * checks that the stats on the monitor are correct
     */
    TEST_P(WriteReadTest, ReadAfterWriteTest){
        SSDConf* ssd_config = base_test_get_ssd_config();

        //writes the whole ssd
        for(unsigned int p=0; p < ssd_config->get_pages(); p++){
              _FTL_WRITE_SECT(p * ssd_config->get_page_size(), 1);
         }
         
         unsigned int time_per_write = REG_WRITE_DELAY + CELL_PROGRAM_DELAY + CHANNEL_SWITCH_DELAY_W;
         
         MONITOR_SYNC_DELAY(ssd_config->get_pages()*(time_per_write));
         
         //reads the whole ssd
         for(unsigned int p=0; p < ssd_config->get_pages(); p++){
              _FTL_READ_SECT(p * ssd_config->get_page_size(), 1);
         }     
         
        unsigned int time_per_read = REG_READ_DELAY + CELL_READ_DELAY + CHANNEL_SWITCH_DELAY_R;

        MONITOR_SYNC_DELAY(time_per_read*ssd_config->get_pages());
        
        double write_speed = CALCULATEMBPS(ssd_config->get_page_size(), time_per_write);
        
        double read_speed = CALCULATEMBPS(ssd_config->get_page_size(), time_per_read);
        
        //checks that log_server.stats (the stats on the monitor) are accurate
        ASSERT_NEAR(write_speed, log_server.stats.write_speed, ERROR_THRESHHOLD(write_speed));
        ASSERT_NEAR(read_speed, log_server.stats.read_speed, ERROR_THRESHHOLD(read_speed));
        ASSERT_EQ(ssd_config->get_pages(), log_server.stats.read_count);
        ASSERT_EQ(ssd_config->get_pages(), log_server.stats.write_count);
        ASSERT_EQ(1, log_server.stats.write_amplification);
        ASSERT_EQ(1.0,log_server.stats.utilization);
    }
    
    /**
     * writes and then reads one page on the ssd 2^n times,
     * checks that the stats on the monitor are correct
     */
    TEST_P(WriteReadTest, WRITEREADTest){
        SSDConf* ssd_config = base_test_get_ssd_config();
        
        //writes and reads pages one at a time      
         
           for(unsigned int p=0; p < ssd_config->get_pages(); p++){
            _FTL_WRITE_SECT(p * ssd_config->get_page_size(), 1);
            _FTL_READ_SECT(p * ssd_config->get_page_size(), 1);
        }

        unsigned int time_per_write = REG_WRITE_DELAY + CELL_PROGRAM_DELAY + CHANNEL_SWITCH_DELAY_R + CELL_READ_DELAY;
        unsigned int time_per_read = REG_READ_DELAY + CELL_READ_DELAY + CHANNEL_SWITCH_DELAY_R;
        
        MONITOR_SYNC_DELAY(ssd_config->get_pages()*(time_per_write+time_per_read));

        MONITOR_SYNC_DELAY(ssd_config->get_pages()*(time_per_write+time_per_read));

        double write_speed = CALCULATEMBPS(ssd_config->get_page_size(), time_per_write);
        
        double read_speed = CALCULATEMBPS(ssd_config->get_page_size(), time_per_read);
        
        //checks that log_server.stats (the stats on the monitor) are accurate
        ASSERT_NEAR(write_speed, log_server.stats.write_speed, ERROR_THRESHHOLD(write_speed));
        ASSERT_NEAR(read_speed, log_server.stats.read_speed, ERROR_THRESHHOLD(read_speed));
        ASSERT_EQ(ssd_config->get_pages(), log_server.stats.read_count);
        ASSERT_EQ(ssd_config->get_pages(), log_server.stats.write_count);
        ASSERT_EQ(1, log_server.stats.write_amplification);
        ASSERT_EQ(1.0, log_server.stats.utilization);
    }
} //namespace
