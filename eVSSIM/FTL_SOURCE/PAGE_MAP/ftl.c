// Copyright(c)2013
//
// Hanyang University, Seoul, Korea
// Embedded Software Systems Lab. All right reserved

#include "common.h"
#include "ftl_sect_strategy.h"
#include "ftl_obj_strategy.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <pthread.h>
#include <unistd.h>

int g_init[MAX_DEVICES] = { 0 };
uint8_t g_device_index = 0;
extern double ssd_util;
int gatherStats = 0;
// Hold statistics information
uint32_t** mapping_stats_table;

void FTL_INIT(uint8_t device_index)
{
	if(g_init[device_index] == 0) {
        PINFO("start\n");
		
		INIT_MAPPING_TABLE(device_index);

		INIT_INVERSE_PAGE_MAPPING(device_index);
		INIT_INVERSE_BLOCK_MAPPING(device_index);
		INIT_VALID_ARRAY(device_index);
		INIT_EMPTY_BLOCK_LIST(device_index);
		INIT_VICTIM_BLOCK_LIST(device_index);

		INIT_PERF_CHECKER();
        INIT_GC_MANAGER();

		// Initialize The Statistics gathering component.
		FTL_INIT_STATS();

		g_init[device_index] = 1;

		SSD_IO_INIT(device_index);

		PINFO("complete\n");
	}
}

void FTL_TERM(uint8_t device_index)
{
	PINFO("start\n");

	TERM_MAPPING_TABLE(device_index);

	TERM_INVERSE_PAGE_MAPPING(device_index);
	TERM_VALID_ARRAY(device_index);
	TERM_INVERSE_BLOCK_MAPPING(device_index);
	TERM_EMPTY_BLOCK_LIST(device_index);
	TERM_VICTIM_BLOCK_LIST(device_index);

	TERM_PERF_CHECKER();
	FTL_TERM_STRATEGY();
	FTL_TERM_STATS();

	SSD_IO_TERM(device_index);
	
	g_init[device_index] = 0;

	PINFO("complete\n");
}

void FTL_TERM_STRATEGY(void)
{
	// As we can't figure out the storage strategy at this point,
	// We can terminate the object strategy anyway... at the worst
	// case where we're actually using the sector strtegy, it won't do
	// anything and return
	TERM_OBJ_STRATEGY();
}

void FTL_INIT_STATS(void)
{
#if 0
TODO: review statistics implementation
	int i,j, thRes;
	struct sockaddr_in serverAddr;
	pthread_t statThread;

	int *clientSock = malloc (sizeof(int));
	/*Creating client socket, in order to receive statistics collection commands from the server*/
	if((*clientSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0){
#ifdef MNT_DEBUG
		printf("[%s] Client Socket Creation error!!!\n",__FUNCTION__);
#endif
	}

	serverAddr.sin_family = AF_INET;
	serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
	serverAddr.sin_port = htons(1234);

	if (connect(*clientSock, &serverAddr , sizeof(serverAddr)) < 0){
#ifdef MNT_DEBUG
		printf("[%s] Error Connection To The Server!\n",__FUNCTION__);
#endif
	}

	/*Creating and starting a thread for communication with the statistic controller*/
	thRes = pthread_create(&statThread, NULL, STAT_LISTEN, (void *) clientSock);

	/* Allocation Memory for Mapping Stats Table */
	mapping_stats_table = (uint32_t**)calloc(SUPPORTED_OPERATION, sizeof(uint32_t*));
	if(mapping_stats_table == NULL){
		printf("ERROR[%s] Calloc mapping stats table fail\n",__FUNCTION__);
		return;
	}
	for (i=0; i < SUPPORTED_OPERATION; i++){
		mapping_stats_table[i] = (uint32_t*)calloc(devices[device_index].page_mapping_entry_nb, sizeof(uint32_t));
		if(mapping_stats_table[i] == NULL){
			printf("ERROR[%s] Calloc mapping stats table fail\n",__FUNCTION__);
			return;
		}
	}

	/* Initialization Mapping Table */
	for(i=0; i < SUPPORTED_OPERATION; i++){
		for(j=0;j<devices[device_index].page_mapping_entry_nb;j++){
			mapping_stats_table[i][j] = 0;
		}
	}
#endif
}

void FTL_RESET_STATS(uint8_t device_index){
	uint64_t i,j;

	for(i=0; i < SUPPORTED_OPERATION; i++){
		for(j=0;j<devices[device_index].page_mapping_entry_nb;j++){
			mapping_stats_table[i][j] = 0;
		}
	}
}

void FTL_TERM_STATS(void){
#if 0
	int i;

	for (i=0; i < SUPPORTED_OPERATION; i++){
		free(mapping_stats_table[i]);
	}
	free(mapping_stats_table);
#endif
}

ftl_ret_val FTL_STATISTICS_GATHERING(uint8_t device_index, uint32_t address , int type){

	if (gatherStats == 0)
	{
		return FTL_SUCCESS;
	}

	if (address > devices[device_index].page_mapping_entry_nb)
	{
		return FTL_FAILURE;
	}

	//Increase the count of the action that was done.
	mapping_stats_table[type][address] = mapping_stats_table[type][address] + 1;

	return FTL_SUCCESS;
}

uint32_t FTL_STATISTICS_QUERY(uint8_t device_index, uint32_t address, int scope , int type){
	//page_nb = is the number of pages in a block, block_nb is the number of blocks in a flash.
	//The first address in the scope start at address 0.

	//int i , j , scopeFirstPage , planeNumber;
	uint32_t i, j;
	uint32_t scopeFirstPage;
	uint32_t actionSum;
	actionSum = 0;

	scopeFirstPage = CALC_SCOPE_FIRST_PAGE(device_index, address, scope);
	switch (scope)
	{
		case PAGE:
			return mapping_stats_table[type][address];
		case BLOCK:
			for (i = 0; i < devices[device_index].page_nb ; i++){
				actionSum += mapping_stats_table[type][scopeFirstPage + i];
			}
			return actionSum;
		case PLANE:
			if (devices[device_index].planes_per_flash > 1){
				for (i = 0; i < devices[device_index].block_nb; i += devices[device_index].planes_per_flash){
					for(j = 0; j < devices[device_index].page_nb ; j++){
						actionSum += mapping_stats_table[type][scopeFirstPage + j];
					}
					scopeFirstPage += devices[device_index].page_nb * devices[device_index].planes_per_flash;
					printf ("%d\n" , scopeFirstPage);
				}
				return actionSum;
			}
			else{
				for (i = 0; i < devices[device_index].pages_per_flash; i++){
					actionSum += mapping_stats_table[type][scopeFirstPage + i];
				}
				return actionSum;
			}
		case FLASH:
			for (i = 0; i < devices[device_index].pages_per_flash; i++){
				actionSum += mapping_stats_table[type][scopeFirstPage + i];
			}
			return actionSum;
		case CHANNEL:
			for (i = 0; i < devices[device_index].flash_nb; i += devices[device_index].channel_nb){
				for(j = 0; j < devices[device_index].pages_per_flash ; j++){
					actionSum += mapping_stats_table[type][scopeFirstPage + j];
				}
				scopeFirstPage += devices[device_index].pages_per_flash * devices[device_index].channel_nb; //Pages in blocks that don't belongs to this plane.
			}
			return actionSum;
		case SSD:
			for (i =0; i < devices[device_index].page_mapping_entry_nb ; i++){
				actionSum += mapping_stats_table[type][i];
			}
			return actionSum;
		default:
			return -1;
	}
}


void FTL_RECORD_STATISTICS(uint8_t device_index){
	int scopeRange , i , queryResult , scope , address;

	FILE* fp = fopen(devices[device_index].stat_path, "w");
	if (fp == NULL)
		RERR(, "File open fail\n");
	//Print required titles
	if (devices[device_index].stat_scope & COLLECT_SSD){
		fprintf(fp , "SSD,");
		scopeRange = 1;
		scope = SSD;
	}
	if (devices[device_index].stat_scope & COLLECT_CHANNEL){
		fprintf(fp , "Channel,");
		scopeRange = devices[device_index].channel_nb;
		scope = CHANNEL;
	}
	if (devices[device_index].stat_scope & COLLECT_FLASH){
		fprintf(fp , "Flash,");
		scopeRange = devices[device_index].flash_nb;
		scope = FLASH;
	}
	if (devices[device_index].stat_scope & COLLECT_PLANE){
		fprintf(fp , "Plane,");
		scopeRange =  devices[device_index].flash_nb * devices[device_index].planes_per_flash;
		scope = PLANE;
	}
	if (devices[device_index].stat_scope & COLLECT_BLOCK){
		fprintf(fp , "Block,");
		scopeRange = (int64_t)devices[device_index].block_nb * (int64_t)devices[device_index].flash_nb;
		scope = BLOCK;
	}
	if (devices[device_index].stat_scope & COLLECT_PAGE){
		fprintf(fp , "Page,");
		scopeRange = devices[device_index].pages_in_ssd;
		scope = PAGE;
	}
	if (devices[device_index].stat_type & COLLECT_LOGICAL_READ){
		fprintf(fp , "Logical Read,");
	}
	if (devices[device_index].stat_type & COLLECT_LOGICAL_WRITE){
		fprintf(fp , "Logical Write,");
	}
	if (devices[device_index].stat_type & COLLECT_PHYSICAL_READ){
		fprintf(fp , "Physical Read,");
	}
	if (devices[device_index].stat_type & COLLECT_PHYSICAL_WRITE){
		fprintf(fp , "Physical Write");
	}
	fprintf(fp , "\n");

	for (i = 0; i < scopeRange ; i++){
		address = CALC_SCOPE_FIRST_PAGE(device_index, i,scope);
		if (devices[device_index].stat_scope & COLLECT_SSD){
			fprintf(fp , "1,");
		}
		if (devices[device_index].stat_scope & COLLECT_CHANNEL){
			fprintf(fp , "%u," , CALC_CHANNEL(device_index, address));
		}
		if (devices[device_index].stat_scope & COLLECT_FLASH){
			fprintf(fp , "%u," , CALC_FLASH(device_index, address));
		}
		if (devices[device_index].stat_scope & COLLECT_PLANE){
			fprintf(fp , "%u," , CALC_PLANE(device_index, address));
		}
		if (devices[device_index].stat_scope & COLLECT_BLOCK){
			fprintf(fp , "%lu,", CALC_BLOCK(device_index, address));
		}
		if (devices[device_index].stat_scope & COLLECT_PAGE){
			fprintf(fp , "%d," , address);
		}
		if (devices[device_index].stat_type & COLLECT_LOGICAL_READ){
			queryResult = FTL_STATISTICS_QUERY(device_index, i, scope , LOGICAL_READ);
			fprintf(fp , "%d," , queryResult);
		}
		if (devices[device_index].stat_type & COLLECT_LOGICAL_WRITE){
			queryResult = FTL_STATISTICS_QUERY(device_index, i, scope, LOGICAL_WRITE);
			fprintf(fp , "%d," ,queryResult);
		}
		if (devices[device_index].stat_type & COLLECT_PHYSICAL_READ){
			queryResult = FTL_STATISTICS_QUERY(device_index, i, scope, PHYSICAL_READ);
			fprintf(fp , "%d,", queryResult);
		}
		if (devices[device_index].stat_type & COLLECT_PHYSICAL_WRITE){
			queryResult = FTL_STATISTICS_QUERY(device_index, i, scope, PHYSICAL_WRITE);
			fprintf(fp , "%d," , queryResult);
		}
		fprintf(fp , "\n");
	}

	fclose(fp);
}

void *STAT_LISTEN(uint8_t device_index, void *socket){
	char buffer[256];
	int sock = (*(int *) socket);
	int stopListen = 1;
	while(stopListen){
		memset(buffer,0,256);
		while (buffer[0] == 0){
			if(read(sock,buffer,255) <= 0){
#ifndef GTEST
				perror("read");
#endif
			}
		}
		printf("buffer %s\n",buffer);
		if (strcmp(buffer, "start") == 0)
		{
			gatherStats = 1;
		}else if (strcmp(buffer, "stop") == 0){
			FTL_RECORD_STATISTICS(device_index);
			FTL_RESET_STATS(device_index);
			gatherStats = 0;
		}else{
			//stopListen = 0;
		}
	}

	pthread_exit(NULL);
}
