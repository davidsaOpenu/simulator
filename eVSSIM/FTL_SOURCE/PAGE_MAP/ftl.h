// Copyright(c)2013 
//
// Hanyang University, Seoul, Korea
// Embedded Software Systems Lab. All right reserved

#ifndef _FTL_H_
#define _FTL_H_

#include "common.h"
extern uint32_t** mapping_stats_table;
void FTL_INIT(void);
void FTL_TERM(void);

void FTL_READ(uint32_t sector_nb, unsigned int length);
void FTL_WRITE(uint32_t sector_nb, unsigned int length);
void FTL_COPYBACK(uint32_t source, uint32_t destination);

int _FTL_READ(uint32_t sector_nb, unsigned int length);
int _FTL_WRITE(uint32_t sector_nb, unsigned int length);
int _FTL_COPYBACK(uint32_t source, uint32_t destination);
void FTL_INIT_STATS(void);
int FTL_STATISTICS_GATHERING(uint32_t page_nb , int type);
uint32_t FTL_STATISTICS_QUERY(uint32_t address, int scope , int type);
void FTL_RECORD_STATISTICS(void);
void FTL_RESET_STATS(void);
void FTL_TERM_STATS(void);
void FTL_RECORD_SCOPE_STAT(FILE* fp , int scope);
void *STAT_LISTEN(void *socket);

#endif
