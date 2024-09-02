// Copyright(c)2013
//
// Hanyang University, Seoul, Korea
// Embedded Software Systems Lab. All right reserved

#ifndef _MAPPING_MANAGER_H_
#define _MAPPING_MANAGER_H_

#include "ftl.h"

extern uint64_t* mapping_table;
extern void* block_table_start;

extern unsigned int flash_index;
extern unsigned int* plane_index;
extern uint64_t* block_index;

void INIT_MAPPING_TABLE(void);
void TERM_MAPPING_TABLE(void);

uint64_t GET_MAPPING_INFO(uint64_t lpn);
ftl_ret_val GET_NEW_PAGE(int mode, uint64_t mapping_index, uint64_t* ppn);
ftl_ret_val DEFAULT_NEXT_PAGE_ALGO(int mode, uint64_t mapping_index, uint64_t* ppn);

int UPDATE_OLD_PAGE_MAPPING(uint64_t lpn);
int UPDATE_NEW_PAGE_MAPPING(uint64_t lpn, uint64_t ppn);
int UPDATE_NEW_PAGE_MAPPING_NO_LOGICAL(uint64_t ppn);

unsigned int CALC_FLASH(uint64_t ppn);
uint64_t CALC_BLOCK(uint64_t ppn);
uint64_t CALC_PAGE(uint64_t ppn);
unsigned int CALC_PLANE(uint64_t ppn);
unsigned int CALC_CHANNEL(uint64_t ppn);
unsigned int CALC_SCOPE_FIRST_PAGE(uint64_t address, int scope);

#endif
