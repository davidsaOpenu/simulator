// Copyright(c)2013 
//
// Hanyang University, Seoul, Korea
// Embedded Software Systems Lab. All right reserved

#ifndef _FTL_H_
#define _FTL_H_

#include <stdint.h>
#include <stdio.h>

typedef enum {FTL_FAILURE, FTL_SUCCESS} ftl_ret_val;

typedef struct {
  uint32_t nsid;
} ftl_ns_desc;

extern uint32_t** mapping_stats_table;
void FTL_INIT(void);
void FTL_TERM(void);

//TODO: get rid of this (we have no init anymore)
void FTL_TERM_STRATEGY(void);

void FTL_INIT_STATS(void);
ftl_ret_val FTL_STATISTICS_GATHERING(uint32_t page_nb , int type);
uint32_t FTL_STATISTICS_QUERY(uint32_t address, int scope , int type);
void FTL_RECORD_STATISTICS(void);
void FTL_RESET_STATS(void);
void FTL_TERM_STATS(void);
void FTL_RECORD_SCOPE_STAT(FILE* fp , int scope);
uint32_t FTL_GET_MAX_NAMESPACE_NB(void);
uint32_t FTL_GET_NAMESPACE_NB(void);
uint32_t FTL_GET_NAMESPACE_SIZE(uint32_t nsid);
void FTL_GET_NAMESPACE_DESCS(ftl_ns_desc *descs);
void *STAT_LISTEN(void *socket);

#endif
