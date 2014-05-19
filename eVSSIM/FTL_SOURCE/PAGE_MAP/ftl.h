// Copyright(c)2013 
//
// Hanyang University, Seoul, Korea
// Embedded Software Systems Lab. All right reserved

#ifndef _FTL_H_
#define _FTL_H_

#include "common.h"
extern int32_t** mapping_stats_table;
void FTL_INIT(void);
void FTL_TERM(void);

void FTL_READ(int32_t sector_nb, unsigned int length);
void FTL_WRITE(int32_t sector_nb, unsigned int length);
void FTL_COPYBACK(int32_t source, int32_t destination);

int _FTL_READ(int32_t sector_nb, unsigned int length);
int _FTL_WRITE(int32_t sector_nb, unsigned int length);
int _FTL_COPYBACK(int32_t source, int32_t destination);
void FTL_INIT_STATS(void);
int FTL_STATISTICS_GATHERING(int32_t page_nb , int type);
int32_t FTL_STATISTICS_QUERY(int32_t address, int scope , int type);
void FTL_RECORD_STATISTICS(void);
void FTL_RESET_STATS(void);
void FTL_TERM_STATS(void);
void FTL_RECORD_SCOPE_STAT(FILE* fp , int scope);
void *STAT_LISTEN(void *socket);

#endif
