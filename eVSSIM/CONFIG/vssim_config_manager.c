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

/* Garbage Collection */
#ifdef PAGE_MAP
double GC_THRESHOLD;			// added by js
int GC_THRESHOLD_BLOCK_NB;		// added by js
int GC_THRESHOLD_BLOCK_NB_EACH;		// added by js
int GC_VICTIM_NB;
#endif

char gFile_Name[1024] = {0,};

void INIT_SSD_CONFIG(void)
{
	FILE* pfData;
	pfData = fopen("./data/ssd.conf", "r");
	
	char* szCommand = NULL;
	
	szCommand = (char*)malloc(1024);
	memset(szCommand, 0x00, 1024);
	if(pfData!=NULL)
	{
		while(fscanf(pfData, "%s", szCommand)!=EOF)
		{
			if(strcmp(szCommand, "FILE_NAME") == 0)
			{
				fscanf(pfData, "%s", gFile_Name);
			}
			else if(strcmp(szCommand, "PAGE_SIZE") == 0)
			{
				fscanf(pfData, "%d", &PAGE_SIZE);
			}
			else if(strcmp(szCommand, "PAGE_NB") == 0)
			{
				fscanf(pfData, "%d", &PAGE_NB);
			}
			else if(strcmp(szCommand, "SECTOR_SIZE") == 0)
			{
				fscanf(pfData, "%d", &SECTOR_SIZE);
			}	
			else if(strcmp(szCommand, "FLASH_NB") == 0)
			{
				fscanf(pfData, "%d", &FLASH_NB);
			}	
			else if(strcmp(szCommand, "BLOCK_NB") == 0)
			{
				fscanf(pfData, "%d", &BLOCK_NB);
			}					
			else if(strcmp(szCommand, "PLANES_PER_FLASH") == 0)
			{
				fscanf(pfData, "%d", &PLANES_PER_FLASH);
			}
			else if(strcmp(szCommand, "REG_WRITE_DELAY") == 0)
			{
				fscanf(pfData, "%d", &REG_WRITE_DELAY);
			}	
			else if(strcmp(szCommand, "CELL_PROGRAM_DELAY") == 0)
			{
				fscanf(pfData, "%d", &CELL_PROGRAM_DELAY);
			}
			else if(strcmp(szCommand, "REG_READ_DELAY") == 0)
			{
				fscanf(pfData, "%d", &REG_READ_DELAY);
			}
			else if(strcmp(szCommand, "CELL_READ_DELAY") == 0)
			{
				fscanf(pfData, "%d", &CELL_READ_DELAY);
			}
			else if(strcmp(szCommand, "BLOCK_ERASE_DELAY") == 0)
			{
				fscanf(pfData, "%d", &BLOCK_ERASE_DELAY);
			}
			else if(strcmp(szCommand, "CHANNEL_SWITCH_DELAY_R") == 0)
			{
				fscanf(pfData, "%d", &CHANNEL_SWITCH_DELAY_R);
			}
			else if(strcmp(szCommand, "CHANNEL_SWITCH_DELAY_W") == 0)
			{
				fscanf(pfData, "%d", &CHANNEL_SWITCH_DELAY_W);
			}
			else if(strcmp(szCommand, "DSM_TRIM_ENABLE") == 0)
			{
				fscanf(pfData, "%d", &DSM_TRIM_ENABLE);
			}
			else if(strcmp(szCommand, "IO_PARALLELISM") == 0)
			{
				fscanf(pfData, "%d", &IO_PARALLELISM);
			}
			else if(strcmp(szCommand, "CHANNEL_NB") == 0)
			{
				fscanf(pfData, "%d", &CHANNEL_NB);
			}
#if defined FTL_MAP_CACHE 
			else if(strcmp(szCommand, "CACHE_IDX_SIZE") == 0)
			{
				fscanf(pfData, "%d", &CACHE_IDX_SIZE);
			}
#endif
#ifdef SSD_WRITE_BUFFER
			else if(strcmp(szCommand, "WRITE_BUFFER_SIZE") == 0)
			{
				fscanf(pfData, "%u", &WRITE_BUFFER_SIZE);
			}
#endif
#if defined FAST_FTL || defined LAST_FTL
			else if(strcmp(szCommand, "LOG_RAND_BLOCK_NB") == 0)
			{
				fscanf(pfData, "%d", &LOG_RAND_BLOCK_NB);
			}	
			else if(strcmp(szCommand, "LOG_SEQ_BLOCK_NB") == 0)
			{
				fscanf(pfData, "%d", &LOG_SEQ_BLOCK_NB);
			}	
#endif
			memset(szCommand, 0x00, 1024);
		}	
		fclose(pfData);

	}

	/* Exception Handler */
	if(FLASH_NB < CHANNEL_NB){
		printf("ERROR[%s] Wrong CHANNEL_NB %d\n",__FUNCTION__, CHANNEL_NB);
		return;
	}
	if(PLANES_PER_FLASH != 1 && PLANES_PER_FLASH % 2 != 0){
		printf("ERROR[%s] Wron PLANAES_PER_FLASH %d\n", __FUNCTION__, PLANES_PER_FLASH);
		return;
	}

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
#endif

	free(szCommand);
}

char* GET_FILE_NAME(void){
	return gFile_Name;
}
