// Copyright(c)2013 
//
// Hanyang University, Seoul, Korea
// Embedded Software Systems Lab. All right reserved

#include "common.h"
#include "qemu-kvm.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <pthread.h>

int g_init = 0;
extern double ssd_util;
int gatherStats = 0;
//Hold statistics information
int32_t** mapping_stats_table;

void FTL_INIT(void)
{
	if(g_init == 0)
	{
        printf("[%s] start\n",__FUNCTION__);

		INIT_SSD_CONFIG();

		INIT_MAPPING_TABLE();
		INIT_INVERSE_PAGE_MAPPING();
		INIT_INVERSE_BLOCK_MAPPING();
		INIT_VALID_ARRAY();
		INIT_EMPTY_BLOCK_LIST();
		INIT_VICTIM_BLOCK_LIST();
		INIT_PERF_CHECKER();
		//Initialize The Statistics gathering component.
		FTL_INIT_STATS();
		
		g_init = 1;

		SSD_IO_INIT();
		
		printf("[%s] complete\n",__FUNCTION__);
	}
}

void FTL_TERM(void)
{
	printf("[%s] start\n",__FUNCTION__);

	TERM_MAPPING_TABLE();
	TERM_INVERSE_PAGE_MAPPING();
	TERM_VALID_ARRAY();
	TERM_INVERSE_BLOCK_MAPPING();
	TERM_EMPTY_BLOCK_LIST();
	TERM_VICTIM_BLOCK_LIST();
	TERM_PERF_CHECKER();
	FTL_TERM_STATS();
	
	printf("[%s] complete\n",__FUNCTION__);
}

void FTL_READ(int32_t sector_nb, unsigned int length)
{
	int ret;

	ret = _FTL_READ(sector_nb, length);
}

void FTL_WRITE(int32_t sector_nb, unsigned int length)
{
	int ret;

	ret = _FTL_WRITE(sector_nb, length);
}

void FTL_COPYBACK(int32_t source, int32_t destination)
{
	int ret;

	ret = _FTL_COPYBACK(source, destination);
}

int _FTL_READ(int32_t sector_nb, unsigned int length)
{
#ifdef FTL_DEBUG
	printf("[%s] Start: sector_nb %d length %u\n",__FUNCTION__, sector_nb, length);
#endif

	if (sector_nb + length > SECTOR_NB)
	{
		printf("Error[FTL_READ] Exceed Sector number\n"); 
		return FAIL;	
	}

	int32_t lpn;
	int32_t ppn;
	int32_t lba = sector_nb;
	unsigned int remain = length;
	unsigned int left_skip = sector_nb % SECTORS_PER_PAGE;
	unsigned int right_skip;
	unsigned int read_sects;

	unsigned int ret = FAIL;
	int read_page_nb = 0;
	int io_page_nb;

	io_alloc_overhead = ALLOC_IO_REQUEST(sector_nb, length, READ, &io_page_nb);

	remain = length;
	lba = sector_nb;
	left_skip = sector_nb % SECTORS_PER_PAGE;

	while (remain > 0)
	{
		if (remain > SECTORS_PER_PAGE - left_skip)
		{
			right_skip = 0;
		}
		else
		{
			right_skip = SECTORS_PER_PAGE - left_skip - remain;
		}
		
		read_sects = SECTORS_PER_PAGE - left_skip - right_skip;

		lpn = lba / (int32_t)SECTORS_PER_PAGE;
		//Send a logical read action being done to the statistics gathering
		FTL_STATISTICS_GATHERING(lpn , LOGICAL_READ);

		ppn = GET_MAPPING_INFO(lpn);

#ifdef FTL_DEBUG
		if (ppn == -1)
		{
			printf("Error[%s] No Mapping info\n",__FUNCTION__);
		}
#endif

		ret = SSD_PAGE_READ(CALC_FLASH(ppn), CALC_BLOCK(ppn), CALC_PAGE(ppn), read_page_nb, READ, io_page_nb);
		//Send a physical read action being done to the statistics gathering
		if (ret == SUCCESS)
		{
			FTL_STATISTICS_GATHERING(ppn , PHYSICAL_READ);
		}

#ifdef FTL_DEBUG
		if (ret == FAIL)
		{
			printf("Error[%s] %u page read fail \n", __FUNCTION__, ppn);
		}
#endif
		read_page_nb++;

		lba += read_sects;
		remain -= read_sects;
		left_skip = 0;
	}

	INCREASE_IO_REQUEST_SEQ_NB();

#ifdef MONITOR_ON
	char szTemp[1024];
	sprintf(szTemp, "READ PAGE %d ", length);
	WRITE_LOG(szTemp);
#endif

#ifdef FTL_DEBUG
	printf("[%s] Complete\n",__FUNCTION__);
#endif

	return ret;
}

int _FTL_WRITE(int32_t sector_nb, unsigned int length)
{
#ifdef FTL_DEBUG
	printf("[%s] Start: sector_nb %d length %u\n",__FUNCTION__, sector_nb, length);
#endif

	int io_page_nb;

	if (sector_nb + length > SECTOR_NB)
	{
		printf("Error[FTL_WRITE] Exceed Sector number\n");
        return FAIL;
    }
	else
	{
		io_alloc_overhead = ALLOC_IO_REQUEST(sector_nb, length, WRITE, &io_page_nb);
	}

	int32_t lba = sector_nb;
	int32_t lpn;
	int32_t new_ppn;

	unsigned int remain = length;
	unsigned int left_skip = sector_nb % SECTORS_PER_PAGE;
	unsigned int right_skip;
	unsigned int write_sects;

	unsigned int ret = FAIL;
	int write_page_nb=0;

	while (remain > 0)
	{
		if (remain > SECTORS_PER_PAGE - left_skip)
		{
			right_skip = 0;
		}
		else
		{
			right_skip = SECTORS_PER_PAGE - left_skip - remain;
		}

		write_sects = SECTORS_PER_PAGE - left_skip - right_skip;

		ret = GET_NEW_PAGE(VICTIM_OVERALL, EMPTY_TABLE_ENTRY_NB, &new_ppn);
		if (ret == FAIL)
		{
			printf("ERROR[FTL_WRITE] Get new page fail \n");
			return FAIL;
		}

		ret = SSD_PAGE_WRITE(CALC_FLASH(new_ppn), CALC_BLOCK(new_ppn), CALC_PAGE(new_ppn), write_page_nb, WRITE, io_page_nb);
		//Send a physical write action being done to the statistics gathering
		if (ret == SUCCESS)
		{
			FTL_STATISTICS_GATHERING(new_ppn , PHYSICAL_WRITE);
		}
		write_page_nb++;

		lpn = lba / (int32_t)SECTORS_PER_PAGE;
		//Send a logical write action being done to the statistics gathering
		FTL_STATISTICS_GATHERING(lpn , LOGICAL_WRITE);
		
		// logical page number to physical. will need to be changed to account for objectid
		UPDATE_OLD_PAGE_MAPPING(lpn);
		UPDATE_NEW_PAGE_MAPPING(lpn, new_ppn);

#ifdef FTL_DEBUG
        if (ret == FAIL)
        {
            printf("Error[FTL_WRITE] %d page write fail \n", new_ppn);
        }
#endif
		lba += write_sects;
		remain -= write_sects;
		left_skip = 0;
	}

	INCREASE_IO_REQUEST_SEQ_NB();

#ifdef GC_ON
	GC_CHECK(CALC_FLASH(new_ppn), CALC_BLOCK(new_ppn));
#endif

#ifdef MONITOR_ON
	char szTemp[1024];
	sprintf(szTemp, "WRITE PAGE %d ", length);
	WRITE_LOG(szTemp);
	sprintf(szTemp, "WB CORRECT %d", write_page_nb);
	WRITE_LOG(szTemp);
#endif

#ifdef FTL_DEBUG
	printf("[%s] Complete\n",__FUNCTION__);
#endif
	return ret;
}

//Get 2 physical page address, the source page which need to be moved to the destination page
int _FTL_COPYBACK(int32_t source, int32_t destination)
{
	int32_t lpn; //The logical page address, the page that being moved.
	unsigned int ret = FAIL;

	//Handle copyback delays
	ret = SSD_PAGE_COPYBACK(source, destination, COPYBACK);

	//Handle page map
	lpn = GET_INVERSE_MAPPING_INFO(source);
	if (lpn != -1)
	{
		//The given physical page is being map, the mapping information need to be changed
		UPDATE_OLD_PAGE_MAPPING(lpn); //as far as i can tell when being called under the gc manage all the actions are being done, but what if will be called from another place?
		UPDATE_NEW_PAGE_MAPPING(lpn, destination);
	}

#ifdef FTL_DEBUG
	if (ret == FAIL)
	{
		printf("Error[%s] %u page copyback fail \n", __FUNCTION__, ppn);
	}
#endif

	return ret;
}

void FTL_INIT_STATS()
{
	int i,j, thRes;
	struct sockaddr_in serverAddr;
	pthread_t *statThread;

	int *clientSock = malloc (sizeof(int));
	/*Creating client socket, in order to receive statistics collection commands from the server*/
	if ((*clientSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
	{
#ifdef MNT_DEBUG
		printf("[%s] Client Socket Creation error!!!\n",__FUNCTION__);
#endif
	}

	serverAddr.sin_family = AF_INET;
	serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
	serverAddr.sin_port = htons(1234);

	if (connect(*clientSock, &serverAddr , sizeof(serverAddr)) < 0)
	{
#ifdef MNT_DEBUG
		printf("[%s] Error Connection To The Server!\n",__FUNCTION__);
#endif
	}

	/*Creating and starting a thread for communication with the statistic controller*/
	thRes = pthread_create(&statThread, NULL, STAT_LISTEN, (void *) clientSock);

	/* Allocation Memory for Mapping Stats Table */
	mapping_stats_table = (int32_t*)calloc(SUPPORTED_OPERATION, sizeof(int32_t*));
	if (mapping_stats_table == NULL)
	{
		printf("ERROR[%s] Calloc mapping stats table fail\n",__FUNCTION__);
		return;
	}
	
	for (i=0; i < SUPPORTED_OPERATION; i++)
	{
		mapping_stats_table[i] = (int32_t*)calloc(PAGE_MAPPING_ENTRY_NB, sizeof(int32_t));
		if (mapping_stats_table[i] == NULL)
		{
			printf("ERROR[%s] Calloc mapping stats table fail\n",__FUNCTION__);
			return;
		}
	}

	/* Initialization Mapping Table */
	for (i=0; i < SUPPORTED_OPERATION; i++)
	{
		for (j=0;j<PAGE_MAPPING_ENTRY_NB;j++)
		{
			mapping_stats_table[i][j] = 0;
		}
	}
}

void FTL_RESET_STATS()
{
	int i,j;

	for (i=0; i < SUPPORTED_OPERATION; i++)
	{
		for (j=0;j<PAGE_MAPPING_ENTRY_NB;j++)
		{
			mapping_stats_table[i][j] = 0;
		}
	}
}

void FTL_TERM_STATS()
{
	int i;

	for (i=0; i < SUPPORTED_OPERATION; i++)
	{
		free(mapping_stats_table[i]);
	}
	
	free(mapping_stats_table);
}

int FTL_STATISTICS_GATHERING(int32_t address, int type)
{
	if (gatherStats == 0)
	{
		return SUCCESS;
	}
	
	if (address > PAGE_MAPPING_ENTRY_NB)
	{
		return FAIL;
	}

	//Increase the count of the action that was done.
	mapping_stats_table[type][address] = mapping_stats_table[type][address] + 1;

	return SUCCESS;
}

int32_t FTL_STATISTICS_QUERY (int32_t address, int scope, int type)
{
	//PAGE_NB = is the number of pages in a block, BLOCK_NB is the number of blocks in a flash.
	//The first address in the scope start at address 0.

	int i , j , scopeFirstPage , planeNumber;
	int32_t actionSum;
	actionSum = 0;

	scopeFirstPage = CALC_SCOPE_FIRST_PAGE(address, scope);
	switch (scope)
	{
		case PAGE:
			return mapping_stats_table[type][address];
		case BLOCK:
			for (i = 0; i < PAGE_NB ; i++)
			{
				actionSum += mapping_stats_table[type][scopeFirstPage + i];
			}
			return actionSum;
		case PLANE:
			if (PLANES_PER_FLASH > 1)
			{
				for (i = 0; i < BLOCK_NB; i += PLANES_PER_FLASH)
				{
					for (j = 0; j < PAGE_NB ; j++)
					{
						actionSum += mapping_stats_table[type][scopeFirstPage + j];
					}
					
					scopeFirstPage += PAGE_NB * PLANES_PER_FLASH;
					printf ("%d\n" , scopeFirstPage);
				}
				return actionSum;
			}
			else
			{
				for (i = 0; i < PAGES_PER_FLASH; i++)
				{
					actionSum += mapping_stats_table[type][scopeFirstPage + i];
				}
				return actionSum;
			}
		case FLASH:
			for (i = 0; i < PAGES_PER_FLASH; i++)
			{
				actionSum += mapping_stats_table[type][scopeFirstPage + i];
			}
			return actionSum;
		case CHANNEL:
			for (i = 0; i < FLASH_NB; i += CHANNEL_NB)
			{
				for(j = 0; j < PAGES_PER_FLASH ; j++)
				{
					actionSum += mapping_stats_table[type][scopeFirstPage + j];
				}
				
				scopeFirstPage += PAGES_PER_FLASH * CHANNEL_NB; //Pages in blocks that don't belongs to this plane.
			}
			return actionSum;
		case SSD:
			for (i =0; i < PAGE_MAPPING_ENTRY_NB; i++)
			{
				actionSum += mapping_stats_table[type][i];
			}
			return actionSum;
		default:
			return -1;
	}
}


void FTL_RECORD_STATISTICS()
{
	int scopeRange , i , queryResult , scope , address;

	FILE* fp = fopen(STAT_PATH,"w");
	if (fp==NULL)
	{
		printf("ERROR[%s] File open fail\n",__FUNCTION__);
		return;
	}
	
	//Print required titles
	if (STAT_SCOPE & COLLECT_SSD)
	{
		fprintf(fp , "SSD,");
		scopeRange = 1;
		scope = SSD;
	}
	if (STAT_SCOPE & COLLECT_CHANNEL)
	{
		fprintf(fp , "Channel,");
		scopeRange = CHANNEL_NB;
		scope = CHANNEL;
	}
	if (STAT_SCOPE & COLLECT_FLASH)
	{
		fprintf(fp , "Flash,");
		scopeRange = FLASH_NB;
		scope = FLASH;
	}
	if (STAT_SCOPE & COLLECT_PLANE)
	{
		fprintf(fp , "Plane,");
		scopeRange =  FLASH_NB * PLANES_PER_FLASH;
		scope = PLANE;
	}
	if (STAT_SCOPE & COLLECT_BLOCK)
	{
		fprintf(fp , "Block,");
		scopeRange = (int64_t)BLOCK_NB * (int64_t)FLASH_NB;
		scope = BLOCK;
	}
	if (STAT_SCOPE & COLLECT_PAGE)
	{
		fprintf(fp , "Page,");
		scopeRange = PAGES_IN_SSD;
		scope = PAGE;
	}
	
	if (STAT_TYPE & COLLECT_LOGICAL_READ)
	{
		fprintf(fp , "Logical Read,");
	}
	if (STAT_TYPE & COLLECT_LOGICAL_WRITE)
	{
		fprintf(fp , "Logical Write,");
	}
	if (STAT_TYPE & COLLECT_PHYSICAL_READ)
	{
		fprintf(fp , "Physical Read,");
	}
	if (STAT_TYPE & COLLECT_PHYSICAL_WRITE)
	{
		fprintf(fp , "Physical Write");
	}
	fprintf(fp , "\n");

	for (i = 0; i < scopeRange ; i++)
	{
		address = CALC_SCOPE_FIRST_PAGE(i,scope);
		if (STAT_SCOPE & COLLECT_SSD)
		{
			fprintf(fp , "1,");
		}
		if (STAT_SCOPE & COLLECT_CHANNEL)
		{
			fprintf(fp , "%d," , CALC_CHANNEL(address));
		}
		if (STAT_SCOPE & COLLECT_FLASH)
		{
			fprintf(fp , "%d," , CALC_FLASH(address));
		}
		if (STAT_SCOPE & COLLECT_PLANE)
		{
			fprintf(fp , "%d," , CALC_PLANE(address));
		}
		if (STAT_SCOPE & COLLECT_BLOCK)
		{
			fprintf(fp , "%d,", CALC_BLOCK(address));
		}
		if (STAT_SCOPE & COLLECT_PAGE)
		{
			fprintf(fp , "%d," , address);
		}
		if (STAT_TYPE & COLLECT_LOGICAL_READ)
		{
			queryResult = FTL_STATISTICS_QUERY(i , scope , LOGICAL_READ);
			fprintf(fp , "%d," , queryResult);
		}
		if (STAT_TYPE & COLLECT_LOGICAL_WRITE)
		{
			queryResult = FTL_STATISTICS_QUERY(i , scope , LOGICAL_WRITE);
			fprintf(fp , "%d," ,queryResult);
		}
		if (STAT_TYPE & COLLECT_PHYSICAL_READ)
		{
			queryResult = FTL_STATISTICS_QUERY(i , scope , PHYSICAL_READ);
			fprintf(fp , "%d,", queryResult);
		}
		if (STAT_TYPE & COLLECT_PHYSICAL_WRITE)
		{
			queryResult = FTL_STATISTICS_QUERY(i , scope , PHYSICAL_WRITE);
			fprintf(fp , "%d," , queryResult);
		}
		fprintf(fp , "\n");
	}

	fclose(fp);
}

void *STAT_LISTEN(void *socket)
{
	char buffer[256];
	int sock = (*(int *) socket);
	int stopListen = 1;
	while (stopListen)
	{
		memset(buffer,0,256);
		while (buffer[0] == 0)
		{
			read(sock,buffer,255);
		}
		printf("buffer %s\n",buffer);
		
		if (strcmp(buffer, "start") == 0)
		{
			gatherStats = 1;
		}
		else if (strcmp(buffer, "stop") == 0)
		{
			FTL_RECORD_STATISTICS();
			FTL_RESET_STATS();
			gatherStats = 0;
		}
		else
		{
			//stopListen = 0;
		}
	}

	pthread_exit(NULL);
}
