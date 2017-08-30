// Copyright(c)2013 
//
// Hanyang University, Seoul, Korea
// Embedded Software Systems Lab. All right reserved

#ifndef _FTL_H_
#define _FTL_H_

#include <stdint.h>
#include <stdio.h>
typedef uint64_t partition_id_t;
typedef unsigned int psize_t; // size in Mb

typedef unsigned int length_t;
typedef unsigned int offset_t;
typedef uint8_t *buf_ptr_t;

typedef enum {FTL_FAILURE, FTL_SUCCESS} ftl_ret_val;
typedef enum {FTL_SECTOR_STRATEGY, FTL_OBJECT_STRATEGY} strategy_t;

extern uint32_t** mapping_stats_table;
void FTL_INIT(strategy_t strategy, partition_id_t part_id, psize_t size);
void FTL_TERM(strategy_t strategy, partition_id_t part_id);


void FTL_INIT_STATS(void);
ftl_ret_val FTL_STATISTICS_GATHERING(uint32_t page_nb , int type);
uint32_t FTL_STATISTICS_QUERY(uint32_t address, int scope , int type);
void FTL_RECORD_STATISTICS(void);
void FTL_RESET_STATS(void);
void FTL_TERM_STATS(void);
void FTL_RECORD_SCOPE_STAT(FILE* fp , int scope);
void *STAT_LISTEN(void *socket);


#endif
