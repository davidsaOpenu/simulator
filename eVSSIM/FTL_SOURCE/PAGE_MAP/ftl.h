// Copyright(c)2013
//
// Hanyang University, Seoul, Korea
// Embedded Software Systems Lab. All right reserved

#ifndef _FTL_H_
#define _FTL_H_

#include <stdint.h>
#include <stdio.h>
#include <pthread.h>

typedef enum {FTL_FAILURE, FTL_SUCCESS} ftl_ret_val;

typedef struct {
  uint32_t nsid;
} ftl_ns_desc;

extern uint32_t** mapping_stats_table;
extern uint8_t g_device_index;
extern int* g_init_ftl;
extern pthread_mutex_t g_lock;

void FTL_INIT(uint8_t device_index);
void FTL_TERM(uint8_t device_index);

void FTL_INIT_STATS(void);
ftl_ret_val FTL_STATISTICS_GATHERING(uint8_t device_index, uint32_t page_nb, int type);
uint32_t FTL_STATISTICS_QUERY(uint8_t device_index, uint32_t address, int scope, int type);
void FTL_RECORD_STATISTICS(uint8_t device_index);
void FTL_RESET_STATS(uint8_t device_index);
void FTL_TERM_STATS(void);
void FTL_RECORD_SCOPE_STAT(FILE* fp, int scope);
void *STAT_LISTEN(uint8_t device_index, void *socket);
void FTL_GET_NAMESPACE_DESCS(uint8_t device_index, ftl_ns_desc *descs, const uint16_t available_ns);

uint32_t FTL_GET_MAX_NAMESPACE_NB(void);
uint32_t FTL_GET_NAMESPACE_NB(uint8_t device_index);
uint32_t FTL_GET_NAMESPACE_SIZE(uint8_t device_index, uint32_t nsid);

#endif // _FTL_H_
