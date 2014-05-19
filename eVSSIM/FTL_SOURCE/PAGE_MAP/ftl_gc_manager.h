// Copyright(c)2013 
//
// Hanyang University, Seoul, Korea
// Embedded Software Systems Lab. All right reserved

#ifndef _GC_MANAGER_H_
#define _GC_MANAGER_H_

extern unsigned int gc_count;

void GC_CHECK(unsigned int phy_flash_nb, unsigned int phy_block_nb);

int GARBAGE_COLLECTION(int mapping_index);
int SELECT_VICTIM_BLOCK(unsigned int* phy_flash_nb, unsigned int* phy_block_nb);

#endif
