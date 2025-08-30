// Copyright(c)2013
//
// Hanyang University, Seoul, Korea
// Embedded Software Systems Lab. All right reserved

#include "common.h"

#define MAPPING_TABLE_INIT_VAL UINT64_MAX

uint64_t* mapping_table[MAX_DEVICES]= { 0 };

extern GCAlgorithm gc_algo;

void INIT_MAPPING_TABLE(uint8_t device_index)
{
	/* Allocation Memory for Mapping Table */
	mapping_table[device_index] = (uint64_t*)calloc((uint64_t)devices[device_index].page_mapping_entry_nb, sizeof(uint64_t));
	if (mapping_table[device_index] == NULL)
		RERR(, "Calloc mapping table fail\n");

	char* data_filename = GET_DATA_FILENAME(device_index, "mapping_table.dat");
	if (data_filename == NULL)
		RERR(, "GET_DATA_FILENAME failed\n");

	FILE* fp = fopen(data_filename, "r");
	free(data_filename);
	if(fp != NULL)
	{
		if(fread(mapping_table[device_index], sizeof(uint64_t), (uint64_t)devices[device_index].page_mapping_entry_nb, fp) <= 0)
			PERR("fread\n");
		fclose(fp);
	}
	else
	{
		uint64_t i;
		for (i=0; i < devices[device_index].page_mapping_entry_nb; i++){
			mapping_table[device_index][i] = MAPPING_TABLE_INIT_VAL;
		}
	}
}

void TERM_MAPPING_TABLE(uint8_t device_index)
{
	char* data_filename = GET_DATA_FILENAME(device_index, "mapping_table.dat");
	if (data_filename == NULL)
		RERR(, "GET_DATA_FILENAME failed\n");

	FILE* fp = fopen(data_filename, "w");
	free(data_filename);
	if (fp == NULL)
		RERR(, "File open fail\n");

	/* Write the mapping table to file */
	if(fwrite(mapping_table[device_index], sizeof(uint64_t), (uint64_t)devices[device_index].page_mapping_entry_nb ,fp) <= 0)
		PERR("fwrite\n");

	/* Free memory for mapping table */
	free(mapping_table[device_index]);
	fclose(fp);
}

uint64_t GET_MAPPING_INFO(uint8_t device_index, uint64_t lpn)
{
	if (lpn >= ((uint64_t)devices[device_index].page_mapping_entry_nb)){
		PERR("overflow!\n");
	}

	const uint64_t ppn = mapping_table[device_index][lpn];
	return ppn;
}

ftl_ret_val GET_NEW_PAGE(uint8_t device_index, int mode, uint64_t mapping_index, uint64_t* ppn)
{
    return gc_algo.next_page(device_index, mode, mapping_index, ppn);
}

ftl_ret_val DEFAULT_NEXT_PAGE_ALGO(uint8_t device_index, int mode, uint64_t mapping_index, uint64_t* ppn)
{
	empty_block_entry* curr_empty_block;

	curr_empty_block = GET_EMPTY_BLOCK(device_index, mode, mapping_index);

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
	*ppn = curr_empty_block->phy_flash_nb*devices[device_index].block_nb*devices[device_index].page_nb \
	       + curr_empty_block->phy_block_nb*devices[device_index].page_nb \
	       + curr_empty_block->curr_phy_page_nb;

	curr_empty_block->curr_phy_page_nb += 1;

	return FTL_SUCCESS;
}

int UPDATE_OLD_PAGE_MAPPING(uint8_t device_index, uint64_t lpn)
{
	uint64_t old_ppn;

	old_ppn = GET_MAPPING_INFO(device_index, lpn);

	if (old_ppn == MAPPING_TABLE_INIT_VAL)

		RDBG_FTL(FTL_FAILURE, "New page \n");

    UPDATE_INVERSE_BLOCK_VALIDITY(device_index,
								  CALC_FLASH(device_index, old_ppn),
                                  CALC_BLOCK(device_index, old_ppn),
                                  CALC_PAGE(device_index, old_ppn),
                                  PAGE_INVALID);
    UPDATE_INVERSE_PAGE_MAPPING(device_index, old_ppn, MAPPING_TABLE_INIT_VAL);

	return FTL_SUCCESS;
}

int UPDATE_NEW_PAGE_MAPPING(uint8_t device_index, uint64_t lpn, uint64_t ppn)
{
	if (lpn >= (uint64_t)devices[device_index].page_mapping_entry_nb){
		PERR("overflow!\n");
	}
	/* Update Page Mapping Table */
	mapping_table[device_index][lpn] = ppn;

	/* Update Inverse Page Mapping Table */
	UPDATE_INVERSE_BLOCK_VALIDITY(device_index, CALC_FLASH(device_index, ppn), CALC_BLOCK(device_index, ppn), CALC_PAGE(device_index, ppn), PAGE_VALID);
	UPDATE_INVERSE_BLOCK_MAPPING(device_index, CALC_FLASH(device_index, ppn), CALC_BLOCK(device_index, ppn), DATA_BLOCK);
	UPDATE_INVERSE_PAGE_MAPPING(device_index, ppn, lpn);

	return FTL_SUCCESS;
}

int UPDATE_NEW_PAGE_MAPPING_NO_LOGICAL(uint8_t device_index, uint64_t ppn)
{
	/* Update Inverse Page Mapping Table */
	UPDATE_INVERSE_BLOCK_VALIDITY(device_index, CALC_FLASH(device_index, ppn), CALC_BLOCK(device_index, ppn), CALC_PAGE(device_index, ppn), PAGE_VALID);
	UPDATE_INVERSE_BLOCK_MAPPING(device_index, CALC_FLASH(device_index, ppn), CALC_BLOCK(device_index, ppn), DATA_BLOCK);

	return FTL_SUCCESS;
}

unsigned int CALC_FLASH(uint8_t device_index, uint64_t ppn)
{
	unsigned int flash_nb = (ppn/devices[device_index].page_nb) / devices[device_index].block_nb;

	if (flash_nb >= devices[device_index].flash_nb) {
		PERR("flash_nb %u\n", flash_nb);
	}
	return flash_nb;
}

uint64_t CALC_BLOCK(uint8_t device_index, uint64_t ppn)
{
	uint64_t block_nb = (ppn/devices[device_index].page_nb) % devices[device_index].block_nb;

	if(block_nb >= devices[device_index].block_nb){
		PERR("block_nb %lu\n", block_nb);
	}
	return block_nb;
}

uint64_t CALC_PAGE(uint8_t device_index, uint64_t ppn)
{
	unsigned int page_nb = ppn % devices[device_index].page_nb;

	return page_nb;
}

unsigned int CALC_PLANE(uint8_t device_index, uint64_t ppn)
{
	int flash_nb = CALC_FLASH(device_index, ppn);
	int block_nb = CALC_BLOCK(device_index, ppn);
	int plane;

	plane = flash_nb * devices[device_index].planes_per_flash + block_nb % devices[device_index].planes_per_flash;

	return plane;
}

unsigned int CALC_CHANNEL(uint8_t device_index, uint64_t ppn)
{
	int flash_nb = CALC_FLASH(device_index, ppn);
	int channel;

	channel = flash_nb % devices[device_index].channel_nb;

	return channel;
}

unsigned int CALC_SCOPE_FIRST_PAGE(uint8_t device_index, uint64_t address, int scope)
{
	int planeNumber;
	switch (scope)
	{
		case PAGE:
			return address;
		case BLOCK:
			return  address * devices[device_index].page_nb;
		case PLANE:
			//If only there is only 1 plane per flash, then continue to flash case which will produce the right result in that case.
			if (devices[device_index].planes_per_flash > 1){
				//Only block that i % devices[device_index].planes_per_flash = planeNumber are in that plane
				planeNumber = address % devices[device_index].planes_per_flash;
				return (((address / devices[device_index].planes_per_flash) * devices[device_index].pages_per_flash) + (devices[device_index].page_nb * planeNumber));
			}else{
				return address * devices[device_index].pages_per_flash;
			}
		case FLASH:
			return address * devices[device_index].pages_per_flash;
		case CHANNEL:
			return devices[device_index].pages_per_flash * address;
		case SSD:
			return 0;
		default:
			return -1;
	}
}
