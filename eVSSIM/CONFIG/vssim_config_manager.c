// Copyright(c)2013 
//
// Hanyang University, Seoul, Korea
// Embedded Software Systems Lab. All right reserved

#include "common.h"

/* SSD Configuration */
int SECTOR_SIZE;
int PAGE_SIZE;

int64_t SECTOR_NB;
int PAGE_NB;
int FLASH_NB;
int BLOCK_NB;
int CHANNEL_NB;
int PLANES_PER_FLASH;

int SECTORS_PER_PAGE;
int PAGES_PER_FLASH;
int64_t PAGES_IN_SSD;

int WAY_NB;

/* Mapping Table */
int DATA_BLOCK_NB;
int64_t BLOCK_MAPPING_ENTRY_NB;		// added by js

#ifdef PAGE_MAP
int64_t PAGE_MAPPING_ENTRY_NB;		// added by js
int64_t EACH_EMPTY_TABLE_ENTRY_NB;	// added by js

int EMPTY_TABLE_ENTRY_NB;		// added by js
int VICTIM_TABLE_ENTRY_NB;		// added by js
#endif

/* NAND Flash Delay */
int REG_WRITE_DELAY;
int CELL_PROGRAM_DELAY;
int REG_READ_DELAY;
int CELL_READ_DELAY;
int BLOCK_ERASE_DELAY;
int CHANNEL_SWITCH_DELAY_W;
int CHANNEL_SWITCH_DELAY_R;

int DSM_TRIM_ENABLE;
int IO_PARALLELISM;

int STAT_SCOPE;
int STAT_TYPE;

/* Garbage Collection */
#ifdef PAGE_MAP
double GC_THRESHOLD;			// added by js
int GC_THRESHOLD_BLOCK_NB;		// added by js
int GC_THRESHOLD_BLOCK_NB_EACH;		// added by js
int GC_VICTIM_NB;
double GC_L2_THRESHOLD;
int GC_L2_THRESHOLD_BLOCK_NB;
#endif

/* Storage strategy (1 = sector-based, 2 = object-based */
int STORAGE_STRATEGY;

char gFile_Name[PATH_MAX] = {0,};
char OSD_PATH[PATH_MAX] = {0,};
char STAT_PATH[PATH_MAX] = {0,};

config_param options[] = {
    {"FILE_NAME", "%s", gFile_Name},
    {"PAGE_SIZE", "%d", &PAGE_SIZE},
    {"PAGE_NB", "%d", &PAGE_NB},
    {"SECTOR_SIZE", "%d", &SECTOR_SIZE},
    {"FLASH_NB", "%d", &FLASH_NB},
    {"BLOCK_NB", "%d", &BLOCK_NB},
    {"PLANES_PER_FLASH", "%d", &PLANES_PER_FLASH},
    {"REG_WRITE_DELAY", "%d", &REG_WRITE_DELAY},
    {"CELL_PROGRAM_DELAY", "%d", &CELL_PROGRAM_DELAY},
    {"REG_READ_DELAY", "%d", &REG_READ_DELAY},
    {"CELL_READ_DELAY", "%d", &CELL_READ_DELAY},
    {"BLOCK_ERASE_DELAY", "%d", &BLOCK_ERASE_DELAY},
    {"CHANNEL_SWITCH_DELAY_R", "%d", &CHANNEL_SWITCH_DELAY_R},
    {"CHANNEL_SWITCH_DELAY_W", "%d", &CHANNEL_SWITCH_DELAY_W},
    {"DSM_TRIM_ENABLE", "%d", &DSM_TRIM_ENABLE},
    {"IO_PARALLELISM", "%d", &IO_PARALLELISM},
    {"CHANNEL_NB", "%d", &CHANNEL_NB},
    {"STAT_TYPE", "%d", &STAT_TYPE},
    {"STAT_SCOPE", "%d", &STAT_SCOPE},
    {"STORAGE_STRATEGY", "%d", &STORAGE_STRATEGY},
#if defined FTL_MAP_CACHE
    {"CACHE_IDX_SIZE", "%d", &CACHE_IDX_SIZE},
#endif
#ifdef SSD_WRITE_BUFFER
    {"WRITE_BUFFER_SIZE", "%u", &WRITE_BUFFER_SIZE},
#endif
#if defined FAST_FTL || defined LAST_FTL
    {"LOG_RAND_BLOCK_NB", "%d", &LOG_RAND_BLOCK_NB},
    {"LOG_SEQ_BLOCK_NB", "%d", &LOG_SEQ_BLOCK_NB},
#endif
    {NULL, NULL, NULL},
};

void INIT_SSD_CONFIG(void)
{
    FILE *pfData = fopen("./data/ssd.conf", "r");
    if (pfData == NULL){
        PERR("Can't open file: ./data/ssd.conf\n");
        exit(1);
    }

    char *szCommand = (char*)malloc(1024);
    int i;
    memset(szCommand, 0x00, 1024);
    while (fscanf(pfData, "%s", szCommand) != EOF){
        if (strcmp(szCommand, "STAT_PATH") == 0){
            if (fgets(STAT_PATH, PATH_MAX, pfData) == NULL)
                RERR(, "Can't read STAT_PATH\n");
            continue;
        }
        if (strcmp(szCommand, "OSD_PATH") == 0){
            if (fgets(OSD_PATH, PATH_MAX, pfData) == NULL)
                RERR(, "Can't read OSD_PATH\n");
            continue;
        }		
        for (i = 0; options[i].name != NULL; i++)
            if (strcmp(szCommand, options[i].name) == 0)
                break;
        if (options[i].name == NULL)
            RERR(, "Wrong option %s\n", szCommand);
        if (fscanf(pfData, options[i].type, options[i].ptr) == EOF)
            RERR(, "Can't read %s\n", szCommand);

        memset(szCommand, 0x00, 1024);
    }
    fclose(pfData);

    /* Exception Handler */
    if (FLASH_NB < CHANNEL_NB)
        RERR(, "Wrong CHANNEL_NB %d\n", CHANNEL_NB);
    if (PLANES_PER_FLASH != 1 && PLANES_PER_FLASH % 2 != 0)
        RERR(, "Wrong PLANAES_PER_FLASH %d\n", PLANES_PER_FLASH);

	/* SSD Configuration */
	SECTORS_PER_PAGE = PAGE_SIZE / SECTOR_SIZE;
	PAGES_PER_FLASH = PAGE_NB * BLOCK_NB;
	SECTOR_NB = (int64_t)SECTORS_PER_PAGE * (int64_t)PAGE_NB * (int64_t)BLOCK_NB * (int64_t)FLASH_NB;

	/* Mapping Table */
	BLOCK_MAPPING_ENTRY_NB = (int64_t)BLOCK_NB * (int64_t)FLASH_NB;
	PAGES_IN_SSD = (int64_t)PAGE_NB * (int64_t)BLOCK_NB * (int64_t)FLASH_NB;

#ifdef PAGE_MAP
	PAGE_MAPPING_ENTRY_NB = (int64_t)PAGE_NB * (int64_t)BLOCK_NB * (int64_t)FLASH_NB;
	EACH_EMPTY_TABLE_ENTRY_NB = (int64_t)BLOCK_NB / (int64_t)PLANES_PER_FLASH;

	EMPTY_TABLE_ENTRY_NB = FLASH_NB * PLANES_PER_FLASH;
	VICTIM_TABLE_ENTRY_NB = FLASH_NB * PLANES_PER_FLASH;

	DATA_BLOCK_NB = BLOCK_NB;
#endif
	
	/* Garbage Collection */
#ifdef PAGE_MAP
	GC_THRESHOLD = 0.3; // 70%
	GC_THRESHOLD_BLOCK_NB = (int)((1-GC_THRESHOLD) * (double)BLOCK_MAPPING_ENTRY_NB);
	GC_THRESHOLD_BLOCK_NB_EACH = (int)((1-GC_THRESHOLD) * (double)EACH_EMPTY_TABLE_ENTRY_NB);
	GC_VICTIM_NB = 1;

    GC_L2_THRESHOLD = 0.1; //90%
	GC_L2_THRESHOLD_BLOCK_NB = (int)((1-GC_L2_THRESHOLD) * (double)BLOCK_MAPPING_ENTRY_NB);
#endif

	free(szCommand);
}

char* GET_FILE_NAME(void){
	return gFile_Name;
}

int GET_SECTOR_SIZE(void){
        return SECTOR_SIZE;
}

int GET_PAGE_SIZE(void){
        return SECTOR_SIZE;
}

