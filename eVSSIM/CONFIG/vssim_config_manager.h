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

extern int GC_LOW_THR;
extern int GC_HI_THR;

/*Statistics*/
extern int STAT_TYPE;
extern int STAT_SCOPE;
extern char STAT_PATH[PATH_MAX];
extern char OSD_PATH[PATH_MAX];
void INIT_SSD_CONFIG(void);
char* GET_FILE_NAME(void);

/* Storage strategy (1 = sector-based, 2 = object-based */
extern int STORAGE_STRATEGY;

typedef struct config_param
{
	const char *name;
	const char *type;
	void *ptr;
} config_param;


/* SSD Configuration - functions */
uint32_t GET_SECTOR_SIZE(void);
uint32_t GET_PAGE_SIZE(void);


#endif
