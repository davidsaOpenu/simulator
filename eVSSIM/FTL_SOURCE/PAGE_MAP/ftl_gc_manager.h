// Copyright(c)2013 
//
// Hanyang University, Seoul, Korea
// Embedded Software Systems Lab. All right reserved

#ifndef _GC_MANAGER_H_
#define _GC_MANAGER_H_

extern unsigned int gc_count;

void GC_CHECK(unsigned int phy_flash_nb, unsigned int phy_block_nb, bool force, bool isObjectStrategy);

int GARBAGE_COLLECTION(int mapping_index, int l2, bool isObjectStrategy);
int SELECT_VICTIM_BLOCK(unsigned int* phy_flash_nb, unsigned int* phy_block_nb);

typedef struct write_amplification_counters
{
	unsigned long logical_block_write_counter;
	unsigned long physical_block_write_counter;
}write_amplification_counters;

extern write_amplification_counters wa_counters;

#endif
