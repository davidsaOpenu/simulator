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


void GC_CHECK(unsigned int phy_flash_nb, unsigned int phy_block_nb, bool force, bool isObjectStrategy)
{
	int i, ret;
	int plane_nb = phy_block_nb % PLANES_PER_FLASH;
	int mapping_index = plane_nb * FLASH_NB + phy_flash_nb;
	
	if(force || total_empty_block_nb < GC_THRESHOLD_BLOCK_NB){
        int l2 = total_empty_block_nb < GC_L2_THRESHOLD_BLOCK_NB;
		for(i=0; i<GC_VICTIM_NB; i++){
			ret = GARBAGE_COLLECTION(mapping_index, l2, isObjectStrategy);
			if(ret == FAILURE){
				break;
			}
		}
	}
}

int GARBAGE_COLLECTION(int mapping_index, int l2, bool isObjectStrategy)
{
	int i;
	int ret;
	uint32_t lpn;
	uint32_t old_ppn;
	uint32_t new_ppn;

	unsigned int victim_phy_flash_nb = FLASH_NB;
	unsigned int victim_phy_block_nb = 0;

	char* valid_array;
    int valid_page_nb;
	int copy_page_nb = 0;

	inverse_block_mapping_entry* inverse_block_entry;

	ret = SELECT_VICTIM_BLOCK(&victim_phy_flash_nb, &victim_phy_block_nb);
	if(ret == FAILURE){
#ifdef FTL_DEBUG
		printf("[%s] There is no available victim block\n",__FUNCTION__);
#endif //FTL_DEBUG
		return FAILURE;
	}

	inverse_block_entry = GET_INVERSE_BLOCK_MAPPING_ENTRY(victim_phy_flash_nb, victim_phy_block_nb);
	valid_array = inverse_block_entry->valid_array;
    valid_page_nb = inverse_block_entry->valid_page_nb;

	for(i=0;i<PAGE_NB;i++){
		if(valid_array[i]=='V'){


// This is original vssim code without copyback            
//            ret = GET_NEW_PAGE(VICTIM_OVERALL, mapping_index, &new_ppn);
//            if(ret == FAIL){
//                printf("ERROR[%s] Get new page fail\n",__FUNCTION__);
//                return FAIL;
//            }
//            SSD_PAGE_READ(victim_phy_flash_nb, victim_phy_block_nb, i, i, GC_READ, -1);
//            SSD_PAGE_WRITE(CALC_FLASH(new_ppn), CALC_BLOCK(new_ppn), CALC_PAGE(new_ppn), i, GC_WRITE, -1);
//
//            old_ppn = victim_phy_flash_nb*PAGES_PER_FLASH + victim_phy_block_nb*PAGE_NB + i;
//
//            lpn = GET_INVERSE_MAPPING_INFO(old_ppn);
//            UPDATE_NEW_PAGE_MAPPING(lpn, new_ppn);
// End of original vssim code without copyback            



			ret = GET_NEW_PAGE(VICTIM_INCHIP, mapping_index, &new_ppn);
            if(ret == FAILURE){
                if(! l2){
				    printf("ERROR[%s]: GET_NEW_PAGE(VICTIM_INCHIP, %d): failed\n",__FUNCTION__, mapping_index);
                    return FAILURE;
                }
                // l2 threshold reached. let's re-write the page
                ret = GET_NEW_PAGE(VICTIM_OVERALL, EMPTY_TABLE_ENTRY_NB, &new_ppn);
                if(ret == FAILURE){
				    printf("ERROR[%s]: GET_NEW_PAGE(VICTIM_OVERALL, EMPTY_TABLE_ENTRY_NB): failed\n",__FUNCTION__);
                    return FAILURE;
                }
                SSD_PAGE_READ(victim_phy_flash_nb, victim_phy_block_nb, i, i, GC_READ, -1);
                SSD_PAGE_WRITE(CALC_FLASH(new_ppn), CALC_BLOCK(new_ppn), CALC_PAGE(new_ppn), i, GC_WRITE, -1);
                old_ppn = victim_phy_flash_nb*PAGES_PER_FLASH + victim_phy_block_nb*PAGE_NB + i;
                lpn = GET_INVERSE_MAPPING_INFO(old_ppn);
                UPDATE_NEW_PAGE_MAPPING(lpn, new_ppn);
            }else{
                // Got new page on-chip, can do copy back
            	if (!isObjectStrategy)
            		ret = _FTL_COPYBACK(victim_phy_flash_nb*PAGES_PER_FLASH + victim_phy_block_nb*PAGE_NB + i , new_ppn);
            	else
            		ret = _FTL_OBJ_COPYBACK(victim_phy_flash_nb*PAGES_PER_FLASH + victim_phy_block_nb*PAGE_NB + i , new_ppn);

                if(ret == FAILURE){
#ifdef FTL_DEBUG
                    printf("ERROR[%s]: failed to copyback\n",__FUNCTION__);
#endif //FTL_DEBUG
                    SSD_PAGE_READ(victim_phy_flash_nb, victim_phy_block_nb, i, i, GC_READ, -1);
                    SSD_PAGE_WRITE(CALC_FLASH(new_ppn), CALC_BLOCK(new_ppn), CALC_PAGE(new_ppn), i, GC_WRITE, -1);
                    old_ppn = victim_phy_flash_nb*PAGES_PER_FLASH + victim_phy_block_nb*PAGE_NB + i;
                    lpn = GET_INVERSE_MAPPING_INFO(old_ppn);
                    UPDATE_NEW_PAGE_MAPPING(lpn, new_ppn);
                }
            }

			copy_page_nb++;
			//if we got this far, it means we copied the page from the victim block to a new one -> meaning, we wrote to that new block so we need to update the relevant counter
			wa_counters.physical_block_write_counter++;

		}
	}

	if(copy_page_nb != valid_page_nb){
		printf("ERROR[GARBAGE_COLLECTION] The number of valid page is not correct copy_page_nb (%d) != valid_page_nb (%d)\n", copy_page_nb, valid_page_nb);
		return FAILURE;
	}

	SSD_BLOCK_ERASE(victim_phy_flash_nb, victim_phy_block_nb);
	//update the physical block write counter as we're deleting the victim block which we're freeing during the GC procedure
	wa_counters.physical_block_write_counter++;
	UPDATE_INVERSE_BLOCK_MAPPING(victim_phy_flash_nb, victim_phy_block_nb, EMPTY_BLOCK);
	INSERT_EMPTY_BLOCK(victim_phy_flash_nb, victim_phy_block_nb);

	gc_count++;

#ifdef MONITOR_ON
	char szTemp[1024];
	sprintf(szTemp, "GC ");
	WRITE_LOG(szTemp);
	sprintf(szTemp, "WB AMP %f", (float)wa_counters.physical_block_write_counter / (float)wa_counters.logical_block_write_counter);
	WRITE_LOG(szTemp);
#endif

	return SUCCESSFUL;
}

/* Greedy Garbage Collection Algorithm */
int SELECT_VICTIM_BLOCK(unsigned int* phy_flash_nb, unsigned int* phy_block_nb)
{
	int i, j;
	int entry_nb = 0;

	victim_block_root* curr_root;
	victim_block_entry* curr_victim_entry;
	victim_block_entry* victim_block = NULL;

	if(total_victim_block_nb == 0){
#ifdef FTL_DEBUG
		printf("ERROR[SELECT_VICTIM_BLOCK] There is no victim block\n");
#endif //FTL_DEBUG
		return FAILURE;
	}

	/* if GC_TRIGGER_OVERALL is defined, then */
	curr_root = (victim_block_root*)victim_block_table_start;

	for(i=0;i<VICTIM_TABLE_ENTRY_NB;i++){

		if(curr_root->victim_block_nb != 0){
			entry_nb = curr_root->victim_block_nb;
			curr_victim_entry = curr_root->next;
			if(victim_block == NULL){
				victim_block = curr_root->next;
			}
		}
		else{
			entry_nb = 0;
		}

		for(j=0;j<entry_nb;j++){
			if(*(victim_block->valid_page_nb) > *(curr_victim_entry->valid_page_nb)){
				victim_block = curr_victim_entry;
			}
			curr_victim_entry = curr_victim_entry->next;
		}
		curr_root += 1;
	}
	if(*(victim_block->valid_page_nb) == PAGE_NB){
		fail_cnt++;
		return FAILURE;
	}

	*phy_flash_nb = victim_block->phy_flash_nb;
	*phy_block_nb = victim_block->phy_block_nb;
	EJECT_VICTIM_BLOCK(victim_block);

	return SUCCESSFUL;
}
