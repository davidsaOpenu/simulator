// Copyright(c)2013 
//
// Hanyang University, Seoul, Korea
// Embedded Software Systems Lab. All right reserved

#ifndef _FTL_H_
#define _FTL_H_

extern uint32_t** mapping_stats_table;
void FTL_INIT(void);
void FTL_TERM(void);

void FTL_INIT_STRATEGY(void);
void FTL_TERM_STRATEGY(void);

void FTL_READ(uint64_t id, unsigned int offset, unsigned int length);
void FTL_WRITE(uint64_t id, unsigned int offset, unsigned int length);
int FTL_COPYBACK(uint32_t source, uint32_t destination);
int FTL_CREATE(size_t size);
void FTL_DELETE(uint64_t id);

void FTL_INIT_STATS(void);
int FTL_STATISTICS_GATHERING(uint32_t page_nb , int type);
uint32_t FTL_STATISTICS_QUERY(uint32_t address, int scope , int type);
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



#endif
