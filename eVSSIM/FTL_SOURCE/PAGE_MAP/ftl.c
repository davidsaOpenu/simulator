// Copyright(c)2013
//
// Hanyang University, Seoul, Korea
// Embedded Software Systems Lab. All right reserved

#include "common.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <pthread.h>
#include <unistd.h>

int g_init = 0;
extern double ssd_util;
int gatherStats = 0;
//Hold statistics information
uint32_t** mapping_stats_table;
storage_strategy_functions* storage_strategy;

void FTL_INIT(void)
{
	if(g_init != 0)
		return;

	PINFO("start\n");

	INIT_SSD_CONFIG();
	INIT_MAPPING_TABLE();
	INIT_INVERSE_PAGE_MAPPING();
	INIT_INVERSE_BLOCK_MAPPING();
	INIT_VALID_ARRAY();
	INIT_EMPTY_BLOCK_LIST();
	INIT_VICTIM_BLOCK_LIST();
	INIT_PERF_CHECKER();
	FTL_INIT_STRATEGY();

	//Initialize The Statistics gathering component.
	FTL_INIT_STATS();

	g_init = 1;

	SSD_IO_INIT();
	PINFO("complete\n");
}

void FTL_TERM(void)
{
	PINFO("start\n");
	TERM_MAPPING_TABLE();
	TERM_INVERSE_PAGE_MAPPING();
	TERM_VALID_ARRAY();
	TERM_INVERSE_BLOCK_MAPPING();
	TERM_EMPTY_BLOCK_LIST();
	TERM_VICTIM_BLOCK_LIST();
	TERM_PERF_CHECKER();
	FTL_TERM_STRATEGY();
	FTL_TERM_STATS();
	SSD_IO_TERM();
	PINFO("complete\n");
}

void FTL_INIT_STRATEGY(void)
{
	storage_strategy = malloc(sizeof(storage_strategy_functions));

	if (STORAGE_STRATEGY == STORAGE_STRATEGY_OBJECT){
		storage_strategy->FTL_READ = _FTL_OBJ_READ;
		storage_strategy->FTL_WRITE = _FTL_OBJ_WRITE;
		storage_strategy->FTL_COPYBACK = _FTL_OBJ_COPYBACK;
		storage_strategy->FTL_CREATE = _FTL_OBJ_CREATE;
		storage_strategy->FTL_DELETE = _FTL_OBJ_DELETE;

		// object strategy needs initializing, unlike current other strategies
		INIT_OBJ_STRATEGY();
	}
	else{ // default behaviour, if strategy was not defined or is invalid or whatever
		storage_strategy->FTL_READ = _FTL_READ;
		storage_strategy->FTL_WRITE = _FTL_WRITE;
		storage_strategy->FTL_COPYBACK = _FTL_COPYBACK;
		storage_strategy->FTL_CREATE = _FTL_CREATE;
		storage_strategy->FTL_DELETE = _FTL_DELETE;
	}
}

void FTL_TERM_STRATEGY(void)
{
	if(STORAGE_STRATEGY == STORAGE_STRATEGY_OBJECT)
		TERM_OBJ_STRATEGY();
	free(storage_strategy);
}

void FTL_READ(uint64_t id, unsigned int offset, unsigned int length)
{
	storage_strategy->FTL_READ(id, offset, length);
}

void FTL_WRITE(uint64_t id, unsigned int offset, unsigned int length)
{
	storage_strategy->FTL_WRITE(id, offset, length);
}

int FTL_COPYBACK(uint32_t source, uint32_t destination)
{
	return storage_strategy->FTL_COPYBACK(source, destination);
}

void FTL_CREATE(size_t size)
{
	storage_strategy->FTL_CREATE(size);
}

void FTL_DELETE(uint64_t id)
{
	storage_strategy->FTL_DELETE(id);
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
		mapping_stats_table[i] = (uint32_t*)calloc(PAGE_MAPPING_ENTRY_NB, sizeof(uint32_t));
		if(mapping_stats_table[i] == NULL){
			printf("ERROR[%s] Calloc mapping stats table fail\n",__FUNCTION__);
			return;
		}
	}

	/* Initialization Mapping Table */
	for(i=0; i < SUPPORTED_OPERATION; i++){
		for(j=0;j<PAGE_MAPPING_ENTRY_NB;j++){
			mapping_stats_table[i][j] = 0;
		}
	}
#endif
}

void FTL_RESET_STATS(void)
{
	int i,j;
	for (i = 0; i < SUPPORTED_OPERATION; i++)
		for (j = 0; j < PAGE_MAPPING_ENTRY_NB; j++)
			mapping_stats_table[i][j] = 0;
}

void FTL_TERM_STATS(void)
{
#if 0
	int i;

	for (i=0; i < SUPPORTED_OPERATION; i++){
		free(mapping_stats_table[i]);
	}
	free(mapping_stats_table);
#endif
}

int FTL_STATISTICS_GATHERING(uint32_t address, int type)
{
	if (gatherStats == 0)
		return SUCCESS;
	if (address > PAGE_MAPPING_ENTRY_NB)
		return FAIL;

	//Increase the count of the action that was done.
	mapping_stats_table[type][address] += 1;
	return SUCCESS;
}

uint32_t FTL_STATISTICS_QUERY(uint32_t address, int scope, int type)
{
	//PAGE_NB = is the number of pages in a block, BLOCK_NB is the number of blocks in a flash.
	//The first address in the scope start at address 0.

	//int i , j , scopeFirstPage , planeNumber;
	int i, j, scopeFirstPage;
	uint32_t actionSum;
	actionSum = 0;

	scopeFirstPage = CALC_SCOPE_FIRST_PAGE(address, scope);
	switch (scope){
		case PAGE:
			return mapping_stats_table[type][address];
		case BLOCK:
			for (i = 0; i < PAGE_NB; i++)
				actionSum += mapping_stats_table[type][scopeFirstPage + i];
			return actionSum;
		case PLANE:
			if (PLANES_PER_FLASH > 1){
				for (i = 0; i < BLOCK_NB; i += PLANES_PER_FLASH){
					for (j = 0; j < PAGE_NB; j++)
						actionSum += mapping_stats_table[type][scopeFirstPage + j];
					scopeFirstPage += PAGE_NB * PLANES_PER_FLASH;
					printf("%d\n", scopeFirstPage);
				}
				return actionSum;
			}
			else{
				for (i = 0; i < PAGES_PER_FLASH; i++)
					actionSum += mapping_stats_table[type][scopeFirstPage + i];
				return actionSum;
			}
		case FLASH:
			for (i = 0; i < PAGES_PER_FLASH; i++)
				actionSum += mapping_stats_table[type][scopeFirstPage + i];
			return actionSum;
		case CHANNEL:
			for (i = 0; i < FLASH_NB; i += CHANNEL_NB){
				for (j = 0; j < PAGES_PER_FLASH; j++)
					actionSum += mapping_stats_table[type][scopeFirstPage + j];
				scopeFirstPage += PAGES_PER_FLASH * CHANNEL_NB; //Pages in blocks that don't belongs to this plane.
			}
			return actionSum;
		case SSD:
			for (i = 0; i < PAGE_MAPPING_ENTRY_NB; i++)
				actionSum += mapping_stats_table[type][i];
			return actionSum;
		default:
			return -1;
	}
}

void FTL_RECORD_STATISTICS(void)
{
	int scopeRange, i, queryResult, scope, address;

	FILE *fp = fopen(STAT_PATH, "w");
	if (fp == NULL)
		RERR(, "File open fail\n");

	//Print required titles
	if (STAT_SCOPE & COLLECT_SSD){
		fprintf(fp, "SSD,");
		scopeRange = 1;
		scope = SSD;
	}
	if (STAT_SCOPE & COLLECT_CHANNEL){
		fprintf(fp, "Channel,");
		scopeRange = CHANNEL_NB;
		scope = CHANNEL;
	}
	if (STAT_SCOPE & COLLECT_FLASH){
		fprintf(fp, "Flash,");
		scopeRange = FLASH_NB;
		scope = FLASH;
	}
	if (STAT_SCOPE & COLLECT_PLANE){
		fprintf(fp, "Plane,");
		scopeRange = PLANES_NB;
		scope = PLANE;
	}
	if (STAT_SCOPE & COLLECT_BLOCK){
		fprintf(fp, "Block,");
		scopeRange = (int64_t)BLOCK_NB * (int64_t)FLASH_NB;
		scope = BLOCK;
	}
	if (STAT_SCOPE & COLLECT_PAGE){
		fprintf(fp, "Page,");
		scopeRange = PAGES_IN_SSD;
		scope = PAGE;
	}
	if (STAT_TYPE & COLLECT_LOGICAL_READ)
		fprintf(fp, "Logical Read,");
	if (STAT_TYPE & COLLECT_LOGICAL_WRITE)
		fprintf(fp, "Logical Write,");
	if (STAT_TYPE & COLLECT_PHYSICAL_READ)
		fprintf(fp, "Physical Read,");
	if (STAT_TYPE & COLLECT_PHYSICAL_WRITE)
		fprintf(fp, "Physical Write");
	fprintf(fp, "\n");

	for (i = 0; i < scopeRange; i++){
		address = CALC_SCOPE_FIRST_PAGE(i, scope);
		if (STAT_SCOPE & COLLECT_SSD)
			fprintf(fp, "1,");
		if (STAT_SCOPE & COLLECT_CHANNEL)
			fprintf(fp, "%d,", CALC_CHANNEL(address));
		if (STAT_SCOPE & COLLECT_FLASH)
			fprintf(fp, "%d,", CALC_FLASH(address));
		if (STAT_SCOPE & COLLECT_PLANE)
			fprintf(fp, "%d,", CALC_PLANE(address));
		if (STAT_SCOPE & COLLECT_BLOCK)
			fprintf(fp, "%d,", CALC_BLOCK(address));
		if (STAT_SCOPE & COLLECT_PAGE)
			fprintf(fp, "%d,", address);
		if (STAT_TYPE & COLLECT_LOGICAL_READ){
			queryResult = FTL_STATISTICS_QUERY(i, scope, LOGICAL_READ);
			fprintf(fp, "%d,", queryResult);
		}
		if (STAT_TYPE & COLLECT_LOGICAL_WRITE){
			queryResult = FTL_STATISTICS_QUERY(i, scope, LOGICAL_WRITE);
			fprintf(fp, "%d,", queryResult);
		}
		if (STAT_TYPE & COLLECT_PHYSICAL_READ){
			queryResult = FTL_STATISTICS_QUERY(i, scope, PHYSICAL_READ);
			fprintf(fp, "%d,", queryResult);
		}
		if (STAT_TYPE & COLLECT_PHYSICAL_WRITE){
			queryResult = FTL_STATISTICS_QUERY(i, scope, PHYSICAL_WRITE);
			fprintf(fp, "%d," , queryResult);
		}
		fprintf(fp, "\n");
	}

	fclose(fp);
}

void *STAT_LISTEN(void *socket)
{
	char buffer[256];
	int sock = (*(int *) socket);
	int stopListen = 1;
	while (stopListen){
		memset(buffer, 0, 256);
		while (buffer[0] == 0){
			if (read(sock, buffer,255) <= 0){
#ifndef GTEST
				perror("read");
#endif
			}
		}
		printf("buffer %s\n", buffer);
		if (strcmp(buffer, "start") == 0)
			gatherStats = 1;
		else if (strcmp(buffer, "stop") == 0){
			FTL_RECORD_STATISTICS();
			FTL_RESET_STATS();
			gatherStats = 0;
		}else{
			//stopListen = 0;
		}
	}

	pthread_exit(NULL);
}
