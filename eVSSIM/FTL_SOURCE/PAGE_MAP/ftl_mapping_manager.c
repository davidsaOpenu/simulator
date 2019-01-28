// Copyright(c)2013 
//
// Hanyang University, Seoul, Korea
// Embedded Software Systems Lab. All right reserved

#include "common.h"

uint32_t* mapping_table;
void* block_table_start;
extern GCAlgorithm gc_algo;

void INIT_MAPPING_TABLE(void)
{
	/* Allocation Memory for Mapping Table */
	mapping_table = (uint32_t*)calloc(PAGE_MAPPING_ENTRY_NB, sizeof(uint32_t));
	if (mapping_table == NULL)
		RERR(, "Calloc mapping table fail\n");

	/* Initialization Mapping Table */
	
	/* If mapping_table.dat file exists */
	FILE* fp = fopen("./data/mapping_table.dat","r");
	if(fp != NULL){
		if(fread(mapping_table, sizeof(uint32_t), PAGE_MAPPING_ENTRY_NB, fp) <= 0)
			PERR("fread\n");
		fclose(fp);
	}
	else{	
		int i;	
		for(i=0;i<PAGE_MAPPING_ENTRY_NB;i++){
			mapping_table[i] = -1;
		}
	}
}

void TERM_MAPPING_TABLE(void)
{
	FILE* fp = fopen("./data/mapping_table.dat","w");
	if (fp == NULL)
		RERR(, "File open fail\n");

	/* Write the mapping table to file */
	if(fwrite(mapping_table, sizeof(uint32_t),PAGE_MAPPING_ENTRY_NB,fp) <= 0)
		PERR("fwrite\n");

	/* Free memory for mapping table */
	free(mapping_table);
	fclose(fp);
}

uint32_t GET_MAPPING_INFO(uint32_t lpn)
{
	uint32_t ppn = mapping_table[lpn];

	return ppn;
}

ftl_ret_val GET_NEW_PAGE(int mode, int mapping_index, uint32_t* ppn)
{
    return gc_algo.next_page(mode, mapping_index, ppn);
}

ftl_ret_val DEFAULT_NEXT_PAGE_ALGO(int mode, int mapping_index, uint32_t* ppn)
{
	empty_block_entry* curr_empty_block;

	curr_empty_block = GET_EMPTY_BLOCK(mode, mapping_index);

	if (curr_empty_block == NULL){
		if (VICTIM_INCHIP == mode)
			RINFO(FTL_FAILURE, "GET_EMPTY_BLOCK fail in VICTIM_INCHIP mode\n")
		else
			RERR(FTL_FAILURE, "GET_EMPTY_BLOCK fail\n");
    }
	//In order to understand what is the SYSTEM WIDE index number of the next available page (this is actually the physical page number)
	//we need to count the number of pages from the beginning of the disk, up until the location of the empty block we received from the "GET_EMPTY_BLOCK"
	//method above:
	//
	//We count the number of pages in all flash chips before us
	//We add to it the number of pages in all blocks before our current block (which was returned by the GET_EMPTY_BLOCK method)
	//and then we add to it the amount of blocks which are already used in our current block
	*ppn = curr_empty_block->phy_flash_nb*BLOCK_NB*PAGE_NB \
	       + curr_empty_block->phy_block_nb*PAGE_NB \
	       + curr_empty_block->curr_phy_page_nb;

	curr_empty_block->curr_phy_page_nb += 1;

	return FTL_SUCCESS;
}

int UPDATE_OLD_PAGE_MAPPING(uint32_t lpn)
{
	uint32_t old_ppn;

	old_ppn = GET_MAPPING_INFO(lpn);

	if (old_ppn == -1)

		RDBG_FTL(FTL_FAILURE, "New page \n");

    UPDATE_INVERSE_BLOCK_VALIDITY(CALC_FLASH(old_ppn),
                                  CALC_BLOCK(old_ppn),
                                  CALC_PAGE(old_ppn),
                                  PAGE_INVALID);
    UPDATE_INVERSE_PAGE_MAPPING(old_ppn, -1);

	return FTL_SUCCESS;
}

int UPDATE_NEW_PAGE_MAPPING(uint32_t lpn, uint32_t ppn)
{
	/* Update Page Mapping Table */
	mapping_table[lpn] = ppn;

	/* Update Inverse Page Mapping Table */
	UPDATE_INVERSE_BLOCK_VALIDITY(CALC_FLASH(ppn), CALC_BLOCK(ppn), CALC_PAGE(ppn), PAGE_VALID);
	UPDATE_INVERSE_BLOCK_MAPPING(CALC_FLASH(ppn), CALC_BLOCK(ppn), DATA_BLOCK);
	UPDATE_INVERSE_PAGE_MAPPING(ppn, lpn);

	return FTL_SUCCESS;
}

int UPDATE_NEW_PAGE_MAPPING_NO_LOGICAL(uint32_t ppn)
{
	/* Update Inverse Page Mapping Table */
	UPDATE_INVERSE_BLOCK_VALIDITY(CALC_FLASH(ppn), CALC_BLOCK(ppn), CALC_PAGE(ppn), PAGE_VALID);
	UPDATE_INVERSE_BLOCK_MAPPING(CALC_FLASH(ppn), CALC_BLOCK(ppn), DATA_BLOCK);

	return FTL_SUCCESS;
}

unsigned int CALC_FLASH(uint32_t ppn)
{
	unsigned int flash_nb = (ppn/PAGE_NB)/BLOCK_NB;

	if(flash_nb >= FLASH_NB){
		PERR("flash_nb %u\n", flash_nb);
	}
	return flash_nb;
}

unsigned int CALC_BLOCK(uint32_t ppn)
{
	unsigned int block_nb = (ppn/PAGE_NB)%BLOCK_NB;

	if(block_nb >= BLOCK_NB){
		PERR("block_nb %u\n", block_nb);
	}
	return block_nb;
}

unsigned int CALC_PAGE(uint32_t ppn)
{
	unsigned int page_nb = ppn%PAGE_NB;

	return page_nb;
}

unsigned int CALC_PLANE(uint32_t ppn)
{
	int flash_nb = CALC_FLASH(ppn);
	int block_nb = CALC_BLOCK(ppn);
	int plane;

	plane = flash_nb*PLANES_PER_FLASH + block_nb%PLANES_PER_FLASH;

	return plane;
}

unsigned int CALC_CHANNEL(uint32_t ppn)
{
	int flash_nb = CALC_FLASH(ppn);
	int channel;

	channel = flash_nb % CHANNEL_NB;

	return channel;
}

unsigned int CALC_SCOPE_FIRST_PAGE(uint32_t address, int scope)
{
	int planeNumber;
	switch (scope)
	{
		case PAGE:
			return address;
		case BLOCK:
			return  address * PAGE_NB;
		case PLANE:
			//If only there is only 1 plane per flash, then continue to flash case which will produce the right result in that case.
			if (PLANES_PER_FLASH > 1){
				//Only block that i % PLANES_PER_FLASH = planeNumber are in that plane
				planeNumber = address % PLANES_PER_FLASH;
				return (((address / PLANES_PER_FLASH) * PAGES_PER_FLASH) + (PAGE_NB * planeNumber));
			}else{
				return address * PAGES_PER_FLASH;
			}
		case FLASH:
			return address * PAGES_PER_FLASH;
		case CHANNEL:
			return PAGES_PER_FLASH * address;
		case SSD:
			return 0;
		default:
			return -1;
	}
}
