#include <cmath>
#include "base_emulator_tests.h"

extern RTLogStatistics *rt_log_stats;
extern LogServer log_server;


#ifndef MONITOR_SYNC_DELAY_USEC
#define MONITOR_SYNC_DELAY_USEC 150000
#endif

#define ERROR_THRESHHOLD(x) x*0.01

#define CALCULATEMBPS(s,t) ((double)s*SECOND_IN_USEC)/MEGABYTE_IN_BYTES/t;

void delay(int expected_duration) {
    usleep(expected_duration + MONITOR_SYNC_DELAY_USEC);
}

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
			
			//_logger = logger_init(ssd_config->get_logger_size());
	};
	
	
	std::vector<SSDConf*> GetTestParams() {
        std::vector<SSDConf*> ssd_configs;

		for(unsigned int i = 1; i < 8;i++){
			ssd_configs.push_back(new SSDConf(4096, 10, 1, DEFAULT_FLASH_NB,pow(2,i), DEFAULT_FLASH_NB));
		}

        return ssd_configs;
    }
    
    INSTANTIATE_TEST_CASE_P(DiskSize, WriteReadTest, ::testing::ValuesIn(GetTestParams()));
    
    TEST_P(WriteReadTest, WRITEOnlyTest){
    	SSDConf* ssd_config = base_test_get_ssd_config();
   
   		//writes the whole ssd
    	for(unsigned int p=0; p < PAGES_IN_SSD; p++){
             _FTL_WRITE_SECT(p * ssd_config->get_page_size(), 1);
        } 	
        
        unsigned int time_per_action = REG_WRITE_DELAY + CELL_PROGRAM_DELAY+CHANNEL_SWITCH_DELAY_W;
        
        delay(PAGES_IN_SSD*(time_per_action));
        
        double write_speed = CALCULATEMBPS(ssd_config->get_page_size(),time_per_action);
        
        
        //checks that log_server.stats (the stats on the monitor) are accurate
        ASSERT_NEAR(write_speed, log_server.stats.write_speed, ERROR_THRESHHOLD(write_speed));
        ASSERT_EQ(0, log_server.stats.read_speed);
        ASSERT_EQ(0, log_server.stats.read_count);
        ASSERT_EQ(PAGES_IN_SSD, log_server.stats.write_count);
        ASSERT_EQ(1, log_server.stats.write_amplification);
        ASSERT_EQ(1.0,log_server.stats.utilization);
        
    }
    
    TEST_P(WriteReadTest, ReadAfterWriteTest){
    	SSDConf* ssd_config = base_test_get_ssd_config();

		//writes the whole ssd
    	for(unsigned int p=0; p < PAGES_IN_SSD; p++){
              _FTL_WRITE_SECT(p * ssd_config->get_page_size(), 1);
         }
         
         unsigned int time_per_write = REG_WRITE_DELAY + CELL_PROGRAM_DELAY + CHANNEL_SWITCH_DELAY_W;
         
         delay(PAGES_IN_SSD*(time_per_write));
         
         //reads the whole ssd
    	 for(unsigned int p=0; p < PAGES_IN_SSD; p++){
              _FTL_READ_SECT(p * ssd_config->get_page_size(), 1);
         } 	
         
        unsigned int time_per_read = REG_READ_DELAY + CELL_READ_DELAY + CHANNEL_SWITCH_DELAY_R;

        delay(time_per_read*PAGES_IN_SSD);
        
        double write_speed = CALCULATEMBPS(ssd_config->get_page_size(), time_per_write);
        
        double read_speed = CALCULATEMBPS(ssd_config->get_page_size(), time_per_read);
        
        //checks that log_server.stats (the stats on the monitor) are accurate
        ASSERT_NEAR(write_speed, log_server.stats.write_speed, ERROR_THRESHHOLD(write_speed));
        ASSERT_NEAR(read_speed, log_server.stats.read_speed, ERROR_THRESHHOLD(read_speed));
        ASSERT_EQ(PAGES_IN_SSD, log_server.stats.read_count);
        ASSERT_EQ(PAGES_IN_SSD, log_server.stats.write_count);
        ASSERT_EQ(1, log_server.stats.write_amplification);
        ASSERT_EQ(1.0,log_server.stats.utilization);
    }
    
    TEST_P(WriteReadTest, WRITEREADTest){
    	SSDConf* ssd_config = base_test_get_ssd_config();
    	
    	//writes and reads pages one at a time  	
    	 
   		for(unsigned int p=0; p < PAGES_IN_SSD; p++){
            _FTL_WRITE_SECT(p * ssd_config->get_page_size(), 1);
          	_FTL_READ_SECT(p * ssd_config->get_page_size(), 1);
        } 	
        unsigned int time_per_write = REG_WRITE_DELAY + CELL_PROGRAM_DELAY + CHANNEL_SWITCH_DELAY_W;
        
        unsigned int time_per_read = REG_READ_DELAY + CELL_READ_DELAY;
        
        delay(PAGES_IN_SSD*(time_per_write+time_per_read));

        double write_speed = CALCULATEMBPS(ssd_config->get_page_size(), time_per_write);
        
        double read_speed = CALCULATEMBPS(ssd_config->get_page_size(), time_per_read);
        
        //checks that log_server.stats (the stats on the monitor) are accurate        
        ASSERT_NEAR(write_speed, log_server.stats.write_speed, ERROR_THRESHHOLD(write_speed));
        ASSERT_NEAR(read_speed, log_server.stats.read_speed, ERROR_THRESHHOLD(read_speed));
        ASSERT_EQ(PAGES_IN_SSD, log_server.stats.read_count);
        ASSERT_EQ(PAGES_IN_SSD, log_server.stats.write_count);
        ASSERT_EQ(1, log_server.stats.write_amplification);
        ASSERT_EQ(1.0, log_server.stats.utilization);
    }
} //namespace
