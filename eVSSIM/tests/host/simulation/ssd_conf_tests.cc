#include "base_emulator_tests.h"
#include "vssim_config_manager.h"

using namespace std;
namespace ssd_conf_tests {
    class SSDConfigTest : public BaseTest {
        public:
            virtual void SetUp() {
                BaseTest::SetUp();
                INIT_LOG_MANAGER(g_device_index);
                pthread_mutex_lock(&g_lock); // prevent the GC thread from running
            }
            virtual void TearDown() {
                pthread_mutex_unlock(&g_lock);
                BaseTest::TearDown(false);
                TERM_LOG_MANAGER(g_device_index);
                TERM_SSD_CONFIG();
            }
    };
    
    std::vector<SSDConf*> GetTestParams() {
        std::vector<SSDConf*> ssd_configs;
        // Add configurations with different parameters
        int map_manager_threads = 4;
        ssd_configs.push_back(new SSDConf(parameters::Allsizemb[0], 1, map_manager_threads));
        return ssd_configs;
    };
    
    INSTANTIATE_TEST_CASE_P(DiskSize, SSDConfigTest, ::testing::ValuesIn(GetTestParams()));
    
    /* Takes a basic config and verifes it, with the new map_manager_threads field.
     * Issues a warning in case the disks underfit. */
    TEST_P(SSDConfigTest, ParseMapBasicConfig) {
        SSDConf* ssd_config = base_test_get_ssd_config();
        
        // Verify regular parameters
        ASSERT_EQ(ssd_config->get_page_size(), devices[g_device_index].page_size);
        ASSERT_EQ(ssd_config->get_page_nb(), devices[g_device_index].page_nb);
        ASSERT_EQ(ssd_config->get_sector_size(), devices[g_device_index].sector_size);
        ASSERT_EQ(ssd_config->get_flash_nb(), devices[g_device_index].flash_nb);
        ASSERT_EQ(ssd_config->get_block_nb(), devices[g_device_index].block_nb);
        ASSERT_EQ(1, devices[g_device_index].planes_per_flash);
        ASSERT_EQ(82, devices[g_device_index].reg_write_delay);
        ASSERT_EQ(900, devices[g_device_index].cell_program_delay);
        ASSERT_EQ(82, devices[g_device_index].reg_read_delay);
        ASSERT_EQ(50, devices[g_device_index].cell_read_delay);
        ASSERT_EQ(2000, devices[g_device_index].block_erase_delay);
        ASSERT_EQ(16, devices[g_device_index].channel_switch_delay_r);
        ASSERT_EQ(33, devices[g_device_index].channel_switch_delay_w);
        ASSERT_EQ(ssd_config->get_channel_nb(), devices[g_device_index].channel_nb);
        ASSERT_EQ(15, devices[g_device_index].stat_type);
        ASSERT_EQ(62, devices[g_device_index].stat_scope);
        ASSERT_EQ(ssd_config->get_storage_strategy(), devices[g_device_index].storage_strategy);
        ASSERT_EQ(20, devices[g_device_index].gc_low_thr);
        ASSERT_EQ(80, devices[g_device_index].gc_hi_thr);
 
        // Verify new mapping manager
 	ASSERT_EQ(ssd_config->get_map_manager_threads(), devices[g_device_index].map_manager_threads);

    	// Namespaces cannot exceed physical backing storage
    	
	uint64_t namespace_size = 0;
	// For this tests there are 3 ns per nvme, should probably change to MAX_NAMESPACE_SIZE.
        for (int i = 0; i < 3; ++i) {
            namespace_size += (uint64_t)devices[g_device_index].namespaces_size[i];
        }
    	uint64_t disk_capacity = devices[g_device_index].flash_nb * devices[g_device_index].block_nb * devices[g_device_index].page_nb;
    	ASSERT_LE(namespace_size, disk_capacity); 

	// Check in case underfit, issue a warning
    	if (namespace_size < disk_capacity) {
    	    uint64_t unallocated_space = disk_capacity - namespace_size;
    	    printf("[  WARNING   ] underfit detected. Unallocated space: %lu sectors (Disk: %lu, Assigned NS: %lu)\n",
            unallocated_space, disk_capacity, namespace_size);
    	}

    }

} // namespace ssd_conf_tests
