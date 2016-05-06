// Copyright(c)2013
//
// Hanyang University, Seoul, Korea
// Embedded Software Systems Lab. All right reserved

#ifndef _GC_MANAGER_H_
#define _GC_MANAGER_H_

void GC_CHECK(unsigned int phy_flash_nb, unsigned int phy_block_nb, bool force);

int GARBAGE_COLLECTION(int mapping_index, int l2);
int SELECT_VICTIM_BLOCK(unsigned int *phy_flash_nb, unsigned int *phy_block_nb);

#endif
