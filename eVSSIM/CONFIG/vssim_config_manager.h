// Copyright(c)2013 
//
// Hanyang University, Seoul, Korea
// Embedded Software Systems Lab. All right reserved

#ifndef _CONFIG_MANAGER_H_
#define _CONFIG_MANAGER_H_

#include <limits.h>

/* SSD Configuration */
extern int eVSSIM_SECTOR_SIZE;
extern int eVSSIM_PAGE_SIZE;

extern int64_t SECTOR_NB;
extern int PAGE_NB;
extern int FLASH_NB;
extern int BLOCK_NB;
extern int CHANNEL_NB;
extern int PLANES_PER_FLASH;

extern int SECTORS_PER_PAGE;
extern int PAGES_PER_FLASH;
extern int64_t PAGES_IN_SSD;

extern int WAY_NB;

/* Mapping Table */
extern int DATA_BLOCK_NB;
extern int64_t BLOCK_MAPPING_ENTRY_NB;		// added by js

#ifdef PAGE_MAP
extern int64_t PAGE_MAPPING_ENTRY_NB; 		// added by js
extern int64_t EACH_EMPTY_TABLE_ENTRY_NB;	// added by js

extern int EMPTY_TABLE_ENTRY_NB;
extern int VICTIM_TABLE_ENTRY_NB;
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
void INIT_SSD_CONFIG(void);
char* GET_FILE_NAME(void);

#endif
