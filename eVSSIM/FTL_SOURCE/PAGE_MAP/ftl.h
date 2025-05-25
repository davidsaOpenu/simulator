// Copyright(c)2013 
//
// Hanyang University, Seoul, Korea
// Embedded Software Systems Lab. All right reserved

#ifndef _FTL_H_
#define _FTL_H_

#include <stdint.h>
#include <stdio.h>

typedef enum {FTL_FAILURE, FTL_SUCCESS} ftl_ret_val;

extern uint32_t** mapping_stats_table;
void FTL_INIT(uint8_t device_index);
void FTL_TERM(void);

//TODO: get rid of this (we have no init anymore)
void FTL_TERM_STRATEGY(void);

void FTL_INIT_STATS(void);
ftl_ret_val FTL_STATISTICS_GATHERING(uint32_t page_nb, int type);
uint32_t FTL_STATISTICS_QUERY(uint32_t address, int scope, int type);
void FTL_RECORD_STATISTICS(void);
void FTL_RESET_STATS(void);
void FTL_TERM_STATS(void);
void FTL_RECORD_SCOPE_STAT(FILE* fp, int scope);
void *STAT_LISTEN(void *socket);

#endif
