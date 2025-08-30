// Copyright(c)2013
//
// Hanyang University, Seoul, Korea
// Embedded Software Systems Lab. All right reserved

#ifndef _CONFIG_MANAGER_H_
#define _CONFIG_MANAGER_H_

#include "common.h"
#include <limits.h>

#define MAX_DEVICE_NAME_LEN 8

typedef struct ssd_config {
	char device_name[MAX_DEVICE_NAME_LEN];
    char file_name[PATH_MAX];

	uint32_t sector_size;
	uint32_t page_size;

	uint64_t page_nb;
	uint32_t flash_nb;
	uint64_t block_nb;
	uint32_t channel_nb;
	uint32_t planes_per_flash;

	uint32_t sectors_per_page;
	uint64_t pages_per_flash;
	uint64_t pages_in_ssd;

	uint32_t way_nb;

	// Mapping Table
	uint32_t data_block_nb;
	uint64_t block_mapping_entry_nb;

#ifdef PAGE_MAP
	uint64_t page_mapping_entry_nb;
	uint64_t each_empty_table_entry_nb;

	uint64_t empty_table_entry_nb;
	uint64_t victim_table_entry_nb;
#endif

	// NAND Flash Delay
	int reg_write_delay;
	int cell_program_delay;
	int reg_read_delay;
	int cell_read_delay;
	int block_erase_delay;
	int channel_switch_delay_r;
	int channel_switch_delay_w;

	int dsm_trim_enable;
	int io_parallelism;

	// Garbage Collection
#ifdef PAGE_MAP
	double gc_threshold;
	int gc_threshold_block_nb;
	int gc_threshold_block_nb_each;
	int gc_victim_nb;
	int gc_l2_threshold_block_nb;
#endif

	int gc_low_thr;
	int gc_hi_thr;

	// Statistics
	int stat_type;
	int stat_scope;
	char stat_path[PATH_MAX];
	char osd_path[PATH_MAX];

	int storage_strategy; // 1 = sector-based, 2 = object-based
} ssd_config_t;

/* NVMe devices manager */
extern ssd_config_t devices[MAX_DEVICES];
extern uint8_t device_count;

void INIT_SSD_CONFIG(void);

/* SSD Configuration - functions */
char* GET_FILE_NAME(uint8_t device_index);
uint32_t GET_SECTOR_SIZE(uint8_t device_index);
uint32_t GET_PAGE_SIZE(uint8_t device_index);
ssd_config_t* GET_DEVICES(void);
char* GET_DATA_FILENAME(uint8_t device_index, const char* filename);

#endif
