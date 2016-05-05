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
int PLANES_NB;

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

/* Storage strategy (1 = address-based, 2 = object-based */
int STORAGE_STRATEGY;

char gFile_Name[PATH_MAX] = {0,};
char STAT_PATH[PATH_MAX] = {0,};

void INIT_SSD_CONFIG(void)
{
	FILE *pfData = fopen("./data/ssd.conf", "r");
	char *szCommand = (char*)malloc(1024);
	memset(szCommand, 0x00, 1024);
	if (pfData != NULL){
		while (fscanf(pfData, "%s", szCommand)!=EOF){
			if (strcmp(szCommand, "FILE_NAME") == 0){
				if (fscanf(pfData, "%s", gFile_Name) == EOF)
					PERR("Cannot read filename\n");
			}
			else if (strcmp(szCommand, "PAGE_SIZE") == 0){
				if (fscanf(pfData, "%d", &PAGE_SIZE) == EOF)
					PERR("Wrong PAGE_SIZE\n");
			}
			else if (strcmp(szCommand, "PAGE_NB") == 0){
				if (fscanf(pfData, "%d", &PAGE_NB) == EOF)
					PERR("Wrong PAGE_NB\n");
			}
			else if (strcmp(szCommand, "SECTOR_SIZE") == 0){
				if (fscanf(pfData, "%d", &SECTOR_SIZE) == EOF)
					PERR("Wrong SECTOR_SIZE\n");
			}
			else if (strcmp(szCommand, "FLASH_NB") == 0){
				if (fscanf(pfData, "%d", &FLASH_NB) == EOF)
					PERR("Wrong FLASH_NB\n");
			}
			else if (strcmp(szCommand, "BLOCK_NB") == 0){
				if (fscanf(pfData, "%d", &BLOCK_NB) == EOF)
					PERR("Wrong BLOCK_NB\n");
			}
			else if (strcmp(szCommand, "PLANES_PER_FLASH") == 0){
				if (fscanf(pfData, "%d", &PLANES_PER_FLASH) == EOF)
					PERR("Wrong PLANES_PER_FLASH\n");
			}
			else if (strcmp(szCommand, "REG_WRITE_DELAY") == 0){
				if (fscanf(pfData, "%d", &REG_WRITE_DELAY) == EOF)
					PERR("Wrong REG_WRITE_DELAY\n");
			}
			else if (strcmp(szCommand, "CELL_PROGRAM_DELAY") == 0){
				if (fscanf(pfData, "%d", &CELL_PROGRAM_DELAY) == EOF)
					PERR("Wrong CELL_PROGRAM_DELAY\n");
			}
			else if (strcmp(szCommand, "REG_READ_DELAY") == 0){
				if (fscanf(pfData, "%d", &REG_READ_DELAY) == EOF)
					PERR("Wrong REG_READ_DELAY\n");
			}
			else if (strcmp(szCommand, "CELL_READ_DELAY") == 0){
				if (fscanf(pfData, "%d", &CELL_READ_DELAY) == EOF)
					PERR("Wrong CELL_READ_DELAY\n");
			}
			else if (strcmp(szCommand, "BLOCK_ERASE_DELAY") == 0){
				if (fscanf(pfData, "%d", &BLOCK_ERASE_DELAY) == EOF)
					PERR("Wrong BLOCK_ERASE_DELAY\n");
			}
			else if (strcmp(szCommand, "CHANNEL_SWITCH_DELAY_R") == 0){
				if (fscanf(pfData, "%d", &CHANNEL_SWITCH_DELAY_R) == EOF)
					PERR("Wrong CHANNEL_SWITCH_DELAY_R\n");
			}
			else if (strcmp(szCommand, "CHANNEL_SWITCH_DELAY_W") == 0){
				if (fscanf(pfData, "%d", &CHANNEL_SWITCH_DELAY_W) == EOF)
					PERR("Wrong CHANNEL_SWITCH_DELAY_W\n");
			}
			else if (strcmp(szCommand, "DSM_TRIM_ENABLE") == 0){
				if (fscanf(pfData, "%d", &DSM_TRIM_ENABLE) == EOF)
					PERR("Wrong DSM_TRIM_ENABLE\n");
			}
			else if (strcmp(szCommand, "IO_PARALLELISM") == 0){
				if (fscanf(pfData, "%d", &IO_PARALLELISM) == EOF)
					PERR("Wrong IO_PARALLELISM\n");
			}
			else if (strcmp(szCommand, "CHANNEL_NB") == 0){
				if (fscanf(pfData, "%d", &CHANNEL_NB) == EOF)
					PERR("Wrong CHANNEL_NB\n");
			}
			else if (strcmp(szCommand, "STAT_TYPE") == 0){
				if (fscanf(pfData, "%d", &STAT_TYPE) == EOF)
					PERR("Wrong STAT_TYPE\n");
			}
			else if (strcmp(szCommand, "STAT_SCOPE") == 0){
				if (fscanf(pfData, "%d", &STAT_SCOPE) == EOF)
					PERR("Wrong STAT_SCOPE\n");
			}
			else if (strcmp(szCommand, "STAT_PATH") == 0){
				//fscanf(pfData, "%s", &STAT_PATH);
				if (fgets(STAT_PATH, PATH_MAX, pfData) == NULL)
					PERR("Wrong STAT_PATH\n");
			}
#if defined FTL_MAP_CACHE
			else if (strcmp(szCommand, "CACHE_IDX_SIZE") == 0){
				if (fscanf(pfData, "%d", &CACHE_IDX_SIZE) == EOF)
					PERR("Wrong CACHE_IDX_SIZE\n");
			}
#endif
#ifdef SSD_WRITE_BUFFER
			else if (strcmp(szCommand, "WRITE_BUFFER_SIZE") == 0){
				if (fscanf(pfData, "%u", &WRITE_BUFFER_SIZE) == EOF)
					PERR("Wrong WRITE_BUFFER_SIZE\n");
			}
#endif
#if defined FAST_FTL || defined LAST_FTL
			else if (strcmp(szCommand, "LOG_RAND_BLOCK_NB") == 0){
				if (fscanf(pfData, "%d", &LOG_RAND_BLOCK_NB) == EOF)
					PERR("Wrong LOG_RAND_BLOCK_NB\n");
			}
			else if (strcmp(szCommand, "LOG_SEQ_BLOCK_NB") == 0){
				if (fscanf(pfData, "%d", &LOG_SEQ_BLOCK_NB) == EOF)
					PERR("Wrong LOG_SEQ_BLOCK_NB\n");
			}
#endif
			else if (strcmp(szCommand, "STORAGE_STRATEGY") == 0){
				if (fscanf(pfData, "%d", &STORAGE_STRATEGY) == EOF)
					PERR("Wrong STORAGE_STRATEGY\n");
			}
			memset(szCommand, 0x00, 1024);
		}
		fclose(pfData);
	}
	else
		PERR("cannot open file: ./data/ssd.conf\n");

	/* Exception Handler */
	if (FLASH_NB < CHANNEL_NB)
		RERR(, "Wrong CHANNEL_NB %d\n", CHANNEL_NB);
	if (PLANES_PER_FLASH != 1 && PLANES_PER_FLASH % 2 != 0)
		RERR(, "Wrong PLANES_PER_FLASH %d\n", PLANES_PER_FLASH);

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
	PLANES_NB = FLASH_NB * PLANES_PER_FLASH;
	EMPTY_TABLE_ENTRY_NB = PLANES_NB;
	VICTIM_TABLE_ENTRY_NB = PLANES_NB;

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

char *GET_FILE_NAME(void)
{
	return gFile_Name;
}
