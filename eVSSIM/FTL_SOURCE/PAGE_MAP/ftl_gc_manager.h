// Copyright(c)2013
//
// Hanyang University, Seoul, Korea
// Embedded Software Systems Lab. All right reserved

#ifndef _GC_MANAGER_H_
#define _GC_MANAGER_H_

#include "ftl.h"

typedef ftl_ret_val (*gc_collection_algo)(uint8_t, int, bool);
typedef ftl_ret_val (*gc_next_page_algo)(uint8_t, int, int, uint64_t*);

typedef struct gc_thread {
    pthread_t tid;
    uint8_t device_index;
    pthread_cond_t gc_signal_cond;
    bool gc_stop_flag;
    uint64_t gc_loop_count;
} gc_thread_t;

extern gc_thread_t *gc_threads;

/**
 * Init gc manger
 */
void INIT_GC_MANAGER(uint8_t device_index);
void TERM_GC_MANAGER(uint8_t device_index);

bool GC_CHECK(uint8_t device_index, bool force, bool background);

/**
 * Default garbage collection algorithm
 */
ftl_ret_val DEFAULT_GC_COLLECTION_ALGO(uint8_t device_index, int l2, bool background);

/**
 * Run garbage collection operation
 */
ftl_ret_val GARBAGE_COLLECTION(uint8_t device_index, int l2, bool background);
ftl_ret_val SELECT_VICTIM_BLOCK(uint8_t device_index, unsigned int* phy_flash_nb, uint64_t* phy_block_nb);

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
