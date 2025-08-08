// Copyright(c)2013
//
// Hanyang University, Seoul, Korea
// Embedded Software Systems Lab. All right reserved

#include "common.h"
#include "ftl_sect_strategy.h"
#include "ftl_obj_strategy.h"

unsigned int gc_count = 0;

int fail_cnt = 0;
extern double ssd_util;

write_amplification_counters wa_counters;
extern ssd_disk ssd;
GCAlgorithm gc_algo;

void INIT_GC_MANAGER(void) {
    gc_algo.collection = &DEFAULT_GC_COLLECTION_ALGO;
    gc_algo.next_page = (gc_next_page_algo) &DEFAULT_NEXT_PAGE_ALGO;
}

void GC_CHECK(uint8_t device_index, unsigned int phy_flash_nb, uint64_t phy_block_nb, bool force, bool isObjectStrategy)
{
	int i, ret;
	int plane_nb = phy_block_nb % devices[device_index].planes_per_flash;
	uint64_t mapping_index = plane_nb * devices[device_index].flash_nb + phy_flash_nb;

	if(force || inverse_mappings_manager[device_index].total_empty_block_nb < (uint64_t)devices[device_index].gc_threshold_block_nb) {
        int l2 = inverse_mappings_manager[device_index].total_empty_block_nb < (uint64_t)devices[device_index].gc_l2_threshold_block_nb;
		for(i=0; i<devices[device_index].gc_victim_nb; i++){
			ret = GARBAGE_COLLECTION(device_index, mapping_index, l2, isObjectStrategy);
			if(ret == FTL_FAILURE){
				break;
			}
		}
	}
}

ftl_ret_val GARBAGE_COLLECTION(uint8_t device_index, uint64_t mapping_index, int l2, bool isObjectStrategy)
{
    return gc_algo.collection(device_index, mapping_index, l2, isObjectStrategy);
}

ftl_ret_val DEFAULT_GC_COLLECTION_ALGO(uint8_t device_index, uint64_t mapping_index, int l2, bool isObjectStrategy)
{
	int ret;
	uint32_t nsid;
	uint64_t i;
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
                GET_INVERSE_MAPPING_INFO(device_index, old_ppn, &nsid, &lpn);
                UPDATE_NEW_PAGE_MAPPING(device_index, nsid, lpn, new_ppn);
            }else{
                // Got new page on-chip, can do copy back

            	if (!isObjectStrategy)
				{
            		ret = _FTL_COPYBACK(device_index, victim_phy_flash_nb * devices[device_index].pages_per_flash + victim_phy_block_nb * devices[device_index].page_nb + i , new_ppn);
				}
            	else
				{
            		ret = _FTL_OBJ_COPYBACK(device_index, victim_phy_flash_nb * devices[device_index].pages_per_flash + victim_phy_block_nb * devices[device_index].page_nb + i , new_ppn);
				}

                if(ret == FTL_FAILURE){
                    PDBG_FTL("failed to copyback\n");
                    SSD_PAGE_READ(device_index, victim_phy_flash_nb, victim_phy_block_nb, i, i, GC_READ);
                    SSD_PAGE_WRITE(device_index, CALC_FLASH(device_index, new_ppn), CALC_BLOCK(device_index, new_ppn), CALC_PAGE(device_index, new_ppn), i, GC_WRITE);
                    old_ppn = victim_phy_flash_nb*devices[device_index].pages_per_flash + victim_phy_block_nb* devices[device_index].page_nb + i;
                    GET_INVERSE_MAPPING_INFO(device_index, old_ppn, &nsid, &lpn);
                    UPDATE_NEW_PAGE_MAPPING(device_index, nsid, lpn, new_ppn);
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

	gc_count++;
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
