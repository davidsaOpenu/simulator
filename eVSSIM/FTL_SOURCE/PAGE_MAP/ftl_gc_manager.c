// Copyright(c)2013 
//
// Hanyang University, Seoul, Korea
// Embedded Software Systems Lab. All right reserved

#include "common.h"

unsigned int gc_count = 0;

int fail_cnt = 0;
extern double ssd_util;

void GC_CHECK(unsigned int phy_flash_nb, unsigned int phy_block_nb)
{
	int i, ret;
	int plane_nb = phy_block_nb % PLANES_PER_FLASH;
	int mapping_index = plane_nb * FLASH_NB + phy_flash_nb;
	
	if(total_empty_block_nb < GC_THRESHOLD_BLOCK_NB){
		for(i=0; i<GC_VICTIM_NB; i++){
			ret = GARBAGE_COLLECTION(mapping_index);
			if(ret == FAIL){
				break;
			}
		}
	}
}

int GARBAGE_COLLECTION(int mapping_index)
{
	int i;
	int ret;
	//int32_t lpn;
	//int32_t old_ppn;
	int32_t new_ppn;

	unsigned int victim_phy_flash_nb = FLASH_NB;
	unsigned int victim_phy_block_nb = 0;

	char* valid_array;
	int copy_page_nb = 0;

	inverse_block_mapping_entry* inverse_block_entry;

	ret = SELECT_VICTIM_BLOCK(&victim_phy_flash_nb, &victim_phy_block_nb);
	if(ret == FAIL){
#ifdef FTL_DEBUG
		printf("[%s] There is no available victim block\n",__FUNCTION__);
#endif
		return FAIL;
	}

	inverse_block_entry = GET_INVERSE_BLOCK_MAPPING_ENTRY(victim_phy_flash_nb, victim_phy_block_nb);
	valid_array = inverse_block_entry->valid_array;

	for(i=0;i<PAGE_NB;i++){
		if(valid_array[i]=='V'){

			ret = GET_NEW_PAGE(VICTIM_INCHIP, mapping_index, &new_ppn);
			if(ret == FAIL){
				printf("ERROR[%s] Get new page fail\n",__FUNCTION__);
				return FAIL;
			}

			ret = _FTL_COPYBACK(victim_phy_flash_nb*PAGES_PER_FLASH + victim_phy_block_nb*PAGE_NB + i , new_ppn);

			if(ret == SUCCESS)
				copy_page_nb++;
			else{
				printf("ERROR[%s] Copyback page\n",__FUNCTION__);
				return FAIL;
			}
		}
	}

	if(copy_page_nb != inverse_block_entry->valid_page_nb){
		printf("ERROR[GARBAGE_COLLECTION] The number of valid page is not correct\n");
		return FAIL;
	}

	SSD_BLOCK_ERASE(victim_phy_flash_nb, victim_phy_block_nb);
	UPDATE_INVERSE_BLOCK_MAPPING(victim_phy_flash_nb, victim_phy_block_nb, EMPTY_BLOCK);
	INSERT_EMPTY_BLOCK(victim_phy_flash_nb, victim_phy_block_nb);

	gc_count++;

#ifdef MONITOR_ON
	char szTemp[1024];
	sprintf(szTemp, "GC ");
	WRITE_LOG(szTemp);
	sprintf(szTemp, "WB AMP %d", copy_page_nb);
	WRITE_LOG(szTemp);
#endif

	return SUCCESS;
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
		printf("ERROR[SELECT_VICTIM_BLOCK] There is no victim block\n");
		return FAIL;
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
			if(victim_block->valid_page_nb > curr_victim_entry->valid_page_nb){
				victim_block = curr_victim_entry;
			}
			curr_victim_entry = curr_victim_entry->next;
		}
		curr_root += 1;
	}
	if(victim_block->valid_page_nb == PAGE_NB){
		fail_cnt++;
		return FAIL;
	}

	*phy_flash_nb = victim_block->phy_flash_nb;
	*phy_block_nb = victim_block->phy_block_nb;
	EJECT_VICTIM_BLOCK(victim_block);

	return SUCCESS;
}
