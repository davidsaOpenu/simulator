// Copyright(c)2013 
//
// Hanyang University, Seoul, Korea
// Embedded Software Systems Lab. All right reserved

#ifndef _GC_MANAGER_H_
#define _GC_MANAGER_H_

#include "ftl.h"

extern unsigned int gc_count;

/**
 * Init gc manger
 */
void INIT_GC_MANAGER(void);
/**
 * Set gc collection algorithm
 */
void GC_SET_COLLETION_ALGO(ftl_ret_val (*gc_collection)(int, int, bool));
/**
 * Set gc get new page algorithm
 */
void GC_SET_GET_NEW_PAGE_ALGO(ftl_ret_val (*get_new_page)(int, int, uint32_t*));

void GC_CHECK(unsigned int phy_flash_nb, unsigned int phy_block_nb, bool force, bool isObjectStrategy);

/**
 * Default garbage collection algorithm
 */
ftl_ret_val GARBAGE_COLLECTION_DEFAULT(int mapping_index, int l2, bool isObjectStrategy);

ftl_ret_val GARBAGE_COLLECTION(int mapping_index, int l2, bool isObjectStrategy);
ftl_ret_val SELECT_VICTIM_BLOCK(unsigned int* phy_flash_nb, unsigned int* phy_block_nb);

typedef struct write_amplification_counters
{
	unsigned long logical_block_write_counter;
	unsigned long physical_block_write_counter;
}write_amplification_counters;

extern write_amplification_counters wa_counters;

/**
 * GC collection algorithm struct
 */
typedef struct {
    /**
     * Pointer to method on GC_COLLECTION execution
     */
    ftl_ret_val (*gc_collection)(int, int, bool);
    /**
     * Pointer to method on GET_NEW_PAGE execution
     */
    ftl_ret_val (*get_new_page)(int, int, uint32_t*);
} GCAlgorithm;

#endif
