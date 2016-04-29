// Copyright(c)2013 
//
// Hanyang University, Seoul, Korea
// Embedded Software Systems Lab. All right reserved

#include "common.h"

void GC_CHECK(unsigned int phy_flash_nb, unsigned int phy_block_nb, bool force)
{
	int i;
	int plane_nb = phy_block_nb % PLANES_PER_FLASH;
	int mapping_index = plane_nb * FLASH_NB + phy_flash_nb;

	if (force || total_empty_block_nb < GC_THRESHOLD_BLOCK_NB){
		int l2 = total_empty_block_nb < GC_L2_THRESHOLD_BLOCK_NB;
		for (i = 0; i < GC_VICTIM_NB; i++)
			if (GARBAGE_COLLECTION(mapping_index, l2) == FAIL)
				break;
	}
}

int GARBAGE_COLLECTION(int mapping_index, int l2)
{
	int i, ret;
	uint32_t lpn, old_ppn, new_ppn;
	unsigned int victim_phy_flash_nb = FLASH_NB;
	unsigned int victim_phy_block_nb = 0;
	int copy_page_nb = 0;

	ret = SELECT_VICTIM_BLOCK(&victim_phy_flash_nb, &victim_phy_block_nb);
	if (ret == FAIL)
		RDBG_FTL(FAIL, "There is no available victim block\n");

	inverse_block_mapping_entry *inverse_block_entry;
	inverse_block_entry = GET_INVERSE_BLOCK_MAPPING_ENTRY(victim_phy_flash_nb, victim_phy_block_nb);
	char *valid_array = inverse_block_entry->valid_array;
	int valid_page_nb = inverse_block_entry->valid_page_nb;

	for (i = 0; i < PAGE_NB; i++){
		if (valid_array[i] == 'V'){


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
			if (ret == FAIL){
				if (!l2)
					RERR(FAIL, "GET_NEW_PAGE(VICTIM_INCHIP, %d): failed\n", mapping_index);
				// l2 threshold reached. let's re-write the page
				ret = GET_NEW_PAGE(VICTIM_OVERALL, EMPTY_TABLE_ENTRY_NB, &new_ppn);
				if (ret == FAIL)
					RERR(FAIL, "GET_NEW_PAGE(VICTIM_OVERALL, EMPTY_TABLE_ENTRY_NB): failed\n");
				SSD_PAGE_READ(victim_phy_flash_nb, victim_phy_block_nb, i, i, GC_READ, -1);
				SSD_PAGE_WRITE(CALC_FLASH(new_ppn), CALC_BLOCK(new_ppn), CALC_PAGE(new_ppn), i, GC_WRITE, -1);
				old_ppn = victim_phy_flash_nb*PAGES_PER_FLASH + victim_phy_block_nb*PAGE_NB + i;
				lpn = GET_INVERSE_MAPPING_INFO(old_ppn);
				UPDATE_NEW_PAGE_MAPPING(lpn, new_ppn);
			}
			else{
				// Got new page on-chip, can do copy back
				ret = FTL_COPYBACK(victim_phy_flash_nb*PAGES_PER_FLASH + victim_phy_block_nb*PAGE_NB + i , new_ppn);
				if(ret == FAIL){
					PDBG_FTL("failed to copyback\n");
					SSD_PAGE_READ(victim_phy_flash_nb, victim_phy_block_nb, i, i, GC_READ, -1);
					SSD_PAGE_WRITE(CALC_FLASH(new_ppn), CALC_BLOCK(new_ppn), CALC_PAGE(new_ppn), i, GC_WRITE, -1);
					old_ppn = victim_phy_flash_nb*PAGES_PER_FLASH + victim_phy_block_nb*PAGE_NB + i;
					lpn = GET_INVERSE_MAPPING_INFO(old_ppn);
					UPDATE_NEW_PAGE_MAPPING(lpn, new_ppn);
				}
			}

			copy_page_nb++;
		}
	}

	if(copy_page_nb != valid_page_nb)
		RERR(FAIL, "Incorrect number of valid page. copy_page_nb (%d) != valid_page_nb (%d)\n", copy_page_nb, valid_page_nb);

	SSD_BLOCK_ERASE(victim_phy_flash_nb, victim_phy_block_nb);
	UPDATE_INVERSE_BLOCK_MAPPING(victim_phy_flash_nb, victim_phy_block_nb, EMPTY_BLOCK);
	INSERT_EMPTY_BLOCK(victim_phy_flash_nb, victim_phy_block_nb);

	WRITE_LOG("GC ");
	WRITE_LOG("WB AMP %d", copy_page_nb);

	return SUCCESS;
}

/* Greedy Garbage Collection Algorithm */
int SELECT_VICTIM_BLOCK(unsigned int *phy_flash_nb, unsigned int *phy_block_nb)
{
	int i, j;
	int entry_nb = 0;

	victim_block_root *curr_root;
	victim_block_entry *curr_victim_entry;
	victim_block_entry *victim_block = NULL;

	if (total_victim_block_nb == 0)
		RDBG_FTL(FAIL, "There is no victim block\n");

	/* if GC_TRIGGER_OVERALL is defined, then */
	curr_root = (victim_block_root*)victim_block_table_start;

	for (i = 0; i < VICTIM_TABLE_ENTRY_NB; i++){
		if (curr_root->victim_block_nb != 0){
			entry_nb = curr_root->victim_block_nb;
			curr_victim_entry = curr_root->next;
			if (victim_block == NULL)
				victim_block = curr_root->next;
		}
		else
			entry_nb = 0;

		for (j = 0; j < entry_nb; j++){
			if (*(victim_block->valid_page_nb) > *(curr_victim_entry->valid_page_nb))
				victim_block = curr_victim_entry;
			curr_victim_entry = curr_victim_entry->next;
		}
		curr_root += 1;
	}
	if (*(victim_block->valid_page_nb) == PAGE_NB)
		return FAIL;

	*phy_flash_nb = victim_block->phy_flash_nb;
	*phy_block_nb = victim_block->phy_block_nb;
	EJECT_VICTIM_BLOCK(victim_block);

	return SUCCESS;
}
