// Copyright(c)2013 
//
// Hanyang University, Seoul, Korea
// Embedded Software Systems Lab. All right reserved

#ifndef _GC_MANAGER_H_
#define _GC_MANAGER_H_

#include "ftl.h"

extern unsigned int gc_count;

typedef ftl_ret_val (*gc_collection_algo)(int, int, bool);
typedef ftl_ret_val (*gc_next_page_algo)(int, int, uint32_t*);

/**
 * Init gc manger
 */
void INIT_GC_MANAGER(void);
/**
 * Set gc collection algorithm
 */
void GC_COLLETION_ALGO(gc_collection_algo algo);
/**
 * Set gc get new page algorithm
 */
void GC_NEXT_PAGE_ALGO(gc_next_page_algo algo);

void GC_CHECK(unsigned int phy_flash_nb, unsigned int phy_block_nb, bool force, bool isObjectStrategy);

/**
 * Default garbage collection algorithm
 */
ftl_ret_val DEFAULT_GC_COLLECTION_ALGO(int mapping_index, int l2, bool isObjectStrategy);

/**
 * Run garbage collection operation
 */
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
    gc_collection_algo collection;
    /**
     * Pointer to method on GET_NEW_PAGE execution
     */
    gc_next_page_algo next_page;
} GCAlgorithm;

#endif
