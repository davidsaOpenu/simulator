// Copyright(c)2013
//
// Hanyang University, Seoul, Korea
// Embedded Software Systems Lab. All right reserved

#ifndef _MAPPING_MANAGER_H_
#define _MAPPING_MANAGER_H_

#include "ftl.h"
#include "common.h"

extern uint64_t* mapping_table[MAX_DEVICES][MAX_NUMBER_OF_NAMESPACES];

void INIT_MAPPING_TABLE(uint8_t device_index);
void TERM_MAPPING_TABLE(uint8_t device_index);

uint64_t GET_MAPPING_INFO(uint8_t device_index, uint32_t nsid, uint64_t lpn);
ftl_ret_val GET_NEW_PAGE(uint8_t device_index, int mode, uint64_t mapping_index, uint64_t* ppn);
ftl_ret_val DEFAULT_NEXT_PAGE_ALGO(uint8_t device_index, int mode, uint64_t mapping_index, uint64_t* ppn);

int UPDATE_OLD_PAGE_MAPPING(uint8_t device_index, uint32_t nsid, uint64_t lpn);
int UPDATE_NEW_PAGE_MAPPING(uint8_t device_index, uint32_t nsid, uint64_t lpn, uint64_t ppn);
int UPDATE_NEW_PAGE_MAPPING_NO_LOGICAL(uint8_t device_index, uint64_t ppn);

unsigned int CALC_FLASH(uint8_t device_index, uint64_t ppn);
uint64_t CALC_BLOCK(uint8_t device_index, uint64_t ppn);
uint64_t CALC_PAGE(uint8_t device_index, uint64_t ppn);
unsigned int CALC_PLANE(uint8_t device_index, uint64_t ppn);
unsigned int CALC_CHANNEL(uint8_t device_index, uint64_t ppn);
unsigned int CALC_SCOPE_FIRST_PAGE(uint8_t device_index, uint64_t address, int scope);

#endif
