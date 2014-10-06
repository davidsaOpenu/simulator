// Copyright(c)2013 
//
// Hanyang University, Seoul, Korea
// Embedded Software Systems Lab. All right reserved

#ifndef _MAPPING_MANAGER_H_
#define _MAPPING_MANAGER_H_

extern uint32_t* mapping_table;
extern void* block_table_start;

extern unsigned int flash_index;
extern unsigned int* plane_index;
extern unsigned int* block_index;

void INIT_MAPPING_TABLE(void);
void TERM_MAPPING_TABLE(void);

uint32_t GET_MAPPING_INFO(uint32_t lpn);
int GET_NEW_PAGE(int mode, int mapping_index, uint32_t* ppn);

int UPDATE_OLD_PAGE_MAPPING(uint32_t lpn);
int UPDATE_NEW_PAGE_MAPPING(uint32_t lpn, uint32_t ppn);
int UPDATE_NEW_PAGE_MAPPING_NO_LOGICAL(uint32_t ppn);

unsigned int CALC_FLASH(uint32_t ppn);
unsigned int CALC_BLOCK(uint32_t ppn);
unsigned int CALC_PAGE(uint32_t ppn);
unsigned int CALC_PLANE(uint32_t ppn);
unsigned int CALC_CHANNEL(uint32_t ppn);
unsigned int CALC_SCOPE_FIRST_PAGE(uint32_t address, int scope);

#endif
