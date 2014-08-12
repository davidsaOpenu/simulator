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

void FTL_INIT_STRATEGY(void);
void FTL_TERM_STRATEGY(void);

void FTL_READ(uint64_t id, unsigned int offset, unsigned int length);
void FTL_WRITE(uint64_t id, unsigned int offset, unsigned int length);
int FTL_COPYBACK(int32_t source, int32_t destination);
void FTL_CREATE(size_t size);
void FTL_DELETE(uint64_t id);

void FTL_INIT_STATS(void);
int FTL_STATISTICS_GATHERING(int32_t page_nb , int type);
int32_t FTL_STATISTICS_QUERY(int32_t address, int scope , int type);
void FTL_RECORD_STATISTICS(void);
void FTL_RESET_STATS(void);
void FTL_TERM_STATS(void);
void FTL_RECORD_SCOPE_STAT(FILE* fp , int scope);
void *STAT_LISTEN(void *socket);

/* Storage strategy function pointers' struct */
typedef struct {
        int (* FTL_READ)(uint64_t, unsigned int, unsigned int);
        int (* FTL_WRITE)(uint64_t, unsigned int, unsigned int);
        int (* FTL_COPYBACK)(int, int);
        int (* FTL_CREATE)(size_t);
        int (* FTL_DELETE)(uint64_t);
} storage_strategy_functions;

#define STORAGE_STRATEGY_ADDRESS    1
#define STORAGE_STRATEGY_OBJECT     2

#endif
