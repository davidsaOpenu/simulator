// Copyright(c)2013
//
// Hanyang University, Seoul, Korea
// Embedded Software Systems Lab. All right reserved

#include "common.h"
#include "ftl_sect_strategy.h"
#include "ftl_obj_strategy.h"
#include <time.h>

int fail_cnt = 0;
extern double ssd_util;

write_amplification_counters wa_counters;
extern ssd_disk ssd;
GCAlgorithm gc_algo = {
    &DEFAULT_GC_COLLECTION_ALGO,
    (gc_next_page_algo) &DEFAULT_NEXT_PAGE_ALGO,
};

gc_thread_t *gc_threads;

static void *GC_BACKGROUND_LOOP(void *arg) {
    gc_thread_t *gc_thread = arg;
    uint8_t device_index = gc_thread->device_index;
    DEV_PINFO(device_index, "background GC thread started\n");

    pthread_mutex_lock(&g_lock);
    while (!gc_thread->gc_stop_flag) {
        gc_thread->gc_loop_count++;
        bool collected;
        uint64_t total_empty_block_nb;
        do {
            collected = GC_CHECK(gc_thread->device_index, false);
            total_empty_block_nb = inverse_mappings_manager[device_index].total_empty_block_nb;
        } while (collected && total_empty_block_nb < devices[device_index].gc_hi_thr_block_nb);

        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        if (total_empty_block_nb >= devices[device_index].gc_low_thr_block_nb) {
            ts.tv_sec += devices[device_index].gc_low_thr_interval_sec;
        } else if (total_empty_block_nb >= devices[device_index].gc_hi_thr_block_nb) {
            ts.tv_sec += devices[device_index].gc_hi_thr_interval_sec;
        } else {
            DEV_PINFO(device_index, "background GC wait for invalid page is not implemented yet\n");
            break;
        }

        if (!gc_thread->gc_stop_flag) {
            pthread_cond_timedwait(&gc_thread->gc_signal_cond, &g_lock, &ts);
            // gc_stop_flag must be rechecked immediately - FTL may be already deinitialized
        }
    }
    pthread_mutex_unlock(&g_lock);

    DEV_PINFO(device_index, "background GC thread stopped\n");
    return NULL;
}

void INIT_GC_MANAGER(uint8_t device_index) {
    if (devices[device_index].storage_strategy == STRATEGY_OBJECT) {
        DEV_PINFO(device_index, "GC of object strategy is not currently supported.\n");
        return;
    }

    gc_thread_t *gc_thread = &gc_threads[device_index];
    gc_thread->device_index = device_index;
    pthread_cond_init(&gc_thread->gc_signal_cond, NULL);
    gc_thread->gc_stop_flag = false;
    gc_thread->gc_loop_count = 0;

    if (0 != pthread_create(&gc_thread->tid, NULL, GC_BACKGROUND_LOOP, gc_thread)) {
        DEV_RERR(, device_index, "failed to create GC background thread\n");
    }

    DEV_PINFO(device_index, "background GC initialized\n");
}

void TERM_GC_MANAGER(uint8_t device_index) {
    if (devices[device_index].storage_strategy == STRATEGY_OBJECT) {
        return;
    }

    gc_thread_t *gc_thread = &gc_threads[device_index];

    gc_thread->gc_stop_flag = true;
    pthread_cond_signal(&gc_thread->gc_signal_cond);
    pthread_mutex_unlock(&g_lock);
    if (0 != pthread_join(gc_thread->tid, NULL)) {
        DEV_PERR(device_index, "failed to join GC background thread\n");
    }
    pthread_mutex_lock(&g_lock);

    pthread_cond_destroy(&gc_thread->gc_signal_cond);
}

bool GC_CHECK(uint8_t device_index, bool force)
{
	int i, ret;
	bool collected = false;

	if(force || inverse_mappings_manager[device_index].total_empty_block_nb < (uint64_t)devices[device_index].gc_threshold_block_nb) {
        int l2 = inverse_mappings_manager[device_index].total_empty_block_nb < (uint64_t)devices[device_index].gc_l2_threshold_block_nb;
		for(i=0; i<devices[device_index].gc_victim_nb; i++){
			ret = GARBAGE_COLLECTION(device_index, l2);
			if(ret == FTL_FAILURE){
				break;
			}
			collected = true;
		}
	}
	return collected;
}

ftl_ret_val GARBAGE_COLLECTION(uint8_t device_index, int l2)
{
    return gc_algo.collection(device_index, l2);
}

ftl_ret_val DEFAULT_GC_COLLECTION_ALGO(uint8_t device_index, int l2)
{
	uint64_t i;
	int ret;
	uint64_t lpn;
	uint64_t old_ppn;
	uint64_t new_ppn;

	unsigned int victim_phy_flash_nb = devices[device_index].flash_nb;
	uint64_t victim_phy_block_nb = 0;

	char* valid_array;
    int valid_page_nb;
	int copy_page_nb = 0;

	inverse_block_mapping_entry* inverse_block_entry;

	ret = SELECT_VICTIM_BLOCK(device_index, &victim_phy_flash_nb, &victim_phy_block_nb);

	if (ret == FTL_FAILURE)
		RDBG_FTL(FTL_FAILURE, "There is no available victim block\n");

	// attempt to find new pages in the same flash as the victim block, for copyback
	int victim_phy_plane_nb = victim_phy_block_nb % devices[device_index].planes_per_flash;
	uint64_t mapping_index = victim_phy_plane_nb * devices[device_index].flash_nb + victim_phy_flash_nb;

	inverse_block_entry = GET_INVERSE_BLOCK_MAPPING_ENTRY(device_index, victim_phy_flash_nb, victim_phy_block_nb);
	valid_array = inverse_block_entry->valid_array;
    valid_page_nb = inverse_block_entry->valid_page_nb;

	for (i = 0; i < devices[device_index].page_nb; i++){
		if (valid_array[i] == PAGE_VALID){


// This is original vssim code without copyback
//            ret = GET_NEW_PAGE(VICTIM_OVERALL, mapping_index, &new_ppn);
//            if(ret == FAIL){
//                printf("ERROR[%s] Get new page fail\n",__FUNCTION__);
//                return FAIL;
//            }
//            SSD_PAGE_READ(victim_phy_flash_nb, victim_phy_block_nb, i, i, GC_READ);
//            SSD_PAGE_WRITE(CALC_FLASH(new_ppn), CALC_BLOCK(new_ppn), CALC_PAGE(new_ppn), i, GC_WRITE);
//
//            old_ppn = victim_phy_flash_nb*PAGES_PER_FLASH + victim_phy_block_nb*PAGE_NB + i;
//
//            lpn = GET_INVERSE_MAPPING_INFO(old_ppn);
//            UPDATE_NEW_PAGE_MAPPING(lpn, new_ppn);
// End of original vssim code without copyback



			ret = GET_NEW_PAGE(device_index, VICTIM_INCHIP, mapping_index, &new_ppn);

            if(ret == FTL_FAILURE){
                if (!l2)
				    RERR(FTL_FAILURE, "GET_NEW_PAGE(VICTIM_INCHIP, %lx): failed\n", mapping_index);
                // l2 threshold reached. let's re-write the page
                ret = GET_NEW_PAGE(device_index, VICTIM_OVERALL, devices[device_index].empty_table_entry_nb, &new_ppn);
                if(ret == FTL_FAILURE)
				    RERR(FTL_FAILURE, "GET_NEW_PAGE(VICTIM_OVERALL, devices[device_index].empty_table_entry_nb): failed\n");

                SSD_PAGE_READ(device_index, victim_phy_flash_nb, victim_phy_block_nb, i, i, GC_READ);
                SSD_PAGE_WRITE(device_index, CALC_FLASH(device_index, new_ppn), CALC_BLOCK(device_index, new_ppn), CALC_PAGE(device_index, new_ppn), i, GC_WRITE);
                old_ppn = victim_phy_flash_nb * devices[device_index].pages_per_flash + victim_phy_block_nb * devices[device_index].page_nb + i;
                GET_INVERSE_MAPPING_INFO(device_index, old_ppn, &lpn);
                UPDATE_NEW_PAGE_MAPPING(device_index, lpn, new_ppn);
            }else{
                // Got new page on-chip, can do copy back

                if (devices[device_index].storage_strategy == STRATEGY_SECTOR)
                {
                    ret = _FTL_COPYBACK(device_index, victim_phy_flash_nb * devices[device_index].pages_per_flash + victim_phy_block_nb * devices[device_index].page_nb + i , new_ppn);
                }
                else if (devices[device_index].storage_strategy == STRATEGY_OBJECT)
                {
                    ret = _FTL_OBJ_COPYBACK(device_index, victim_phy_flash_nb * devices[device_index].pages_per_flash + victim_phy_block_nb * devices[device_index].page_nb + i , new_ppn);
                }
                else
                {
                    ret = FTL_FAILURE;
                }

                if(ret == FTL_FAILURE){
                    PDBG_FTL("failed to copyback\n");
                    SSD_PAGE_READ(device_index, victim_phy_flash_nb, victim_phy_block_nb, i, i, GC_READ);
                    SSD_PAGE_WRITE(device_index, CALC_FLASH(device_index, new_ppn), CALC_BLOCK(device_index, new_ppn), CALC_PAGE(device_index, new_ppn), i, GC_WRITE);
                    old_ppn = victim_phy_flash_nb*devices[device_index].pages_per_flash + victim_phy_block_nb* devices[device_index].page_nb + i;
                    GET_INVERSE_MAPPING_INFO(device_index, old_ppn, &lpn);
                    UPDATE_NEW_PAGE_MAPPING(device_index, lpn, new_ppn);
                }
            }

			copy_page_nb++;
			//if we got this far, it means we copied the page from the victim block to a new one -> meaning, we wrote to that new block so we need to update the relevant counter
			wa_counters.physical_block_write_counter++;

		}
	}

	if (copy_page_nb != valid_page_nb)

		RERR(FTL_FAILURE, "The number of valid page is not correct copy_page_nb (%d) != valid_page_nb (%d)\n", copy_page_nb, valid_page_nb);


	SSD_BLOCK_ERASE(device_index, victim_phy_flash_nb, victim_phy_block_nb);
	//update the physical block write counter as we're deleting the victim block which we're freeing during the GC procedure
	wa_counters.physical_block_write_counter++;
	UPDATE_INVERSE_BLOCK_MAPPING(device_index, victim_phy_flash_nb, victim_phy_block_nb, EMPTY_BLOCK);
	INSERT_EMPTY_BLOCK(device_index, victim_phy_flash_nb, victim_phy_block_nb);

	LOG_GARBAGE_COLLECTION(GET_LOGGER(device_index, victim_phy_flash_nb), (GarbageCollectionLog) empty_log);

	return FTL_SUCCESS;
}


/* Greedy Garbage Collection Algorithm */
ftl_ret_val SELECT_VICTIM_BLOCK(uint8_t device_index, unsigned int* phy_flash_nb, uint64_t* phy_block_nb)
{
	uint32_t i;

	victim_block_root* curr_root;
	victim_block_entry* curr_victim_entry = NULL;
	victim_block_entry* victim_block = NULL;

	if (inverse_mappings_manager[device_index].total_victim_block_nb == 0)

		RDBG_FTL(FTL_FAILURE, "[SELECT_VICTIM_BLOCK] There is no victim block\n");


	/* if GC_TRIGGER_OVERALL is defined, then */
	curr_root = inverse_mappings_manager[device_index].victim_block_table_start;

	for (i=0;i<devices[device_index].victim_table_entry_nb;i++) {

		if(curr_root->victim_block_nb != 0){
			curr_victim_entry = curr_root->next;
			if(victim_block == NULL){
				victim_block = curr_root->next;
			}
			if(*(victim_block->valid_page_nb) == 0){ // evict dead block
				break;
			}

			if(*(victim_block->valid_page_nb) > *(curr_victim_entry->valid_page_nb)){ // list is sorted by valid_page_nb
				victim_block = curr_victim_entry;
			}
			curr_victim_entry = curr_victim_entry->next;

		}
		curr_root += 1;
	}
	if (*(victim_block->valid_page_nb) == devices[device_index].page_nb) {
		fail_cnt++;
		return FTL_FAILURE;
	}
	*phy_flash_nb = victim_block->phy_flash_nb;
	*phy_block_nb = victim_block->phy_block_nb;
	EJECT_VICTIM_BLOCK(device_index, victim_block);

	return FTL_SUCCESS;
}
