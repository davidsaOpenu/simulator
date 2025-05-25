// Copyright(c)2013
//
// Hanyang University, Seoul, Korea
// Embedded Software Systems Lab. All right reserved

#ifndef _CONFIG_MANAGER_H_
#define _CONFIG_MANAGER_H_

#include "common.h"
#include <limits.h>

/* SSD Configuration */
extern uint64_t SECTOR_NB;
extern uint64_t PAGE_NB;
extern uint32_t FLASH_NB;
extern uint64_t BLOCK_NB;
extern uint32_t CHANNEL_NB;
extern uint32_t PLANES_PER_FLASH;

extern uint32_t SECTORS_PER_PAGE;
extern uint64_t PAGES_PER_FLASH;
extern uint64_t PAGES_IN_SSD;

extern uint32_t WAY_NB;

/* Mapping Table */
extern uint32_t DATA_BLOCK_NB;
extern uint64_t BLOCK_MAPPING_ENTRY_NB;		// added by js

#ifdef PAGE_MAP
extern uint64_t PAGE_MAPPING_ENTRY_NB; 		// added by js
extern uint64_t EACH_EMPTY_TABLE_ENTRY_NB;	// added by js

extern uint64_t EMPTY_TABLE_ENTRY_NB;
extern uint64_t VICTIM_TABLE_ENTRY_NB;
#endif

/* NAND Flash Delay */
extern int REG_WRITE_DELAY;
extern int CELL_PROGRAM_DELAY;
extern int REG_READ_DELAY;
extern int CELL_READ_DELAY;
extern int BLOCK_ERASE_DELAY;
extern int CHANNEL_SWITCH_DELAY_R;
extern int CHANNEL_SWITCH_DELAY_W;

extern int DSM_TRIM_ENABLE;
extern int IO_PARALLELISM;

/* Garbage Collection */
#ifdef PAGE_MAP
extern double GC_THRESHOLD;			// added by js
extern int GC_THRESHOLD_BLOCK_NB;		// added by js
extern int GC_THRESHOLD_BLOCK_NB_EACH;
extern int GC_VICTIM_NB;
extern int GC_L2_THRESHOLD_BLOCK_NB;
#endif

/*Statistics*/
extern int STAT_TYPE;
extern int STAT_SCOPE;
extern char STAT_PATH[PATH_MAX];
extern char OSD_PATH[PATH_MAX];
void INIT_SSD_CONFIG(void);

/* Storage strategy (1 = sector-based, 2 = object-based */
extern int STORAGE_STRATEGY;

typedef struct config_param
{
	const char *name;
	const char *type;
	void *ptr;
} config_param;

typedef struct ssd_config {
	char device_name[MAX_DEVICE_NAME_LEN];
    char file_name[PATH_MAX];
    
	uint32_t sector_size;
	uint32_t page_size;

	uint64_t sector_nb;
	uint64_t page_nb;
	uint32_t flash_nb;
	uint64_t block_nb;
	uint32_t channel_nb;
	uint32_t planes_per_flash;

	uint32_t sectors_per_page;
	uint64_t pages_per_flash;
	uint64_t pages_in_ssd;
	
	uint32_t way_nb;

	uint32_t data_block_nb;
	uint64_t block_mapping_entry_nb;

#ifdef PAGE_MAP
	uint64_t page_mapping_entry_nb;
	uint64_t each_empty_table_entry_nb;

	uint64_t empty_table_entry_nb;
	uint64_t victim_table_entry_nb;
#endif

	int reg_write_delay;
	int cell_program_delay;
	int reg_read_delay;
	int cell_read_delay;
	int block_erase_delay;
	int channel_switch_delay_r;
	int channel_switch_delay_w;

	int dsm_trim_enable;
	int io_parallelism;

#ifdef PAGE_MAP
	double gc_threshold;
	int gc_threshold_block_nb;
	int gc_threshold_block_nb_each;
	int gc_victim_nb;
	int gc_l2_threshold_block_nb;
#endif

	int stat_type;
	int stat_scope;
	char stat_path[PATH_MAX];
	char osd_path[PATH_MAX];

	int storage_strategy;
} ssd_config_t;

/* NVMe devices manager */
extern ssd_config_t devices[MAX_DEVICES];
extern int device_count;
extern int current_device_index;

/* SSD Configuration - functions */
char* GET_FILE_NAME(uint8_t device_index);
uint32_t GET_SECTOR_SIZE(void);
uint32_t GET_PAGE_SIZE(void);
ssd_config_t* GET_DEVICES(void);
void CHANGE_DEVICE(uint8_t device_index);
char* GET_DATA_FILENAME(const char* filename);

#endif
