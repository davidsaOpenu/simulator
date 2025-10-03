// Copyright(c)2013
//
// Hanyang University, Seoul, Korea
// Embedded Software Systems Lab. All right reserved

#include "common.h"
#include "ftl_inverse_mapping_manager.h"

inverse_mapping_manager_t* inverse_mappings_manager = NULL;

void INIT_INVERSE_PAGE_MAPPING(uint8_t device_index)
{
	/* Allocation Memory for Inverse Page Mapping Table */
	inverse_mappings_manager[device_index].inverse_page_mapping_table = calloc(devices[device_index].page_mapping_entry_nb, sizeof(uint64_t));
	if (inverse_mappings_manager[device_index].inverse_page_mapping_table == NULL)
		RERR(, "Calloc mapping table fail\n");

	/* Initialization Inverse Page Mapping Table */
	char* filename = GET_DATA_FILENAME(device_index, "inverse_page_mapping.dat");
	if (filename == NULL)
		RERR(, "GET_DATA_FILENAME failed\n");

	FILE* fp = fopen(filename, "r");
	free(filename);
	if (READ_MAPPING_INFO_FROM_FILES && fp != NULL) {
		if(fread(inverse_mappings_manager[device_index].inverse_page_mapping_table, sizeof(uint64_t), devices[device_index].page_mapping_entry_nb, fp) <= 0)
			PERR("fread\n");
		fclose(fp);
	}
	else{
		uint64_t i;
		for(i=0; i < devices[device_index].page_mapping_entry_nb; i++){
			inverse_mappings_manager[device_index].inverse_page_mapping_table[i] = MAPPING_TABLE_INIT_VAL;
		}
	}
}

void INIT_INVERSE_PAGE_NAMESPACE_MAPPING(uint8_t device_index)
{
	/* Allocation Memory for Inverse Page Mapping Table */
	inverse_mappings_manager[device_index].inverse_page_mapping_namespace_table = (void*)calloc(devices[device_index].page_mapping_entry_nb, sizeof(uint32_t));
	if (inverse_mappings_manager[device_index].inverse_page_mapping_namespace_table == NULL)
		RERR(, "Calloc mapping table fail\n");

	/* Initialization Inverse Page Mapping Table */
	char* filename = GET_DATA_FILENAME(device_index, "inverse_page_mapping_namespace.dat");
	if (filename == NULL)
		RERR(, "GET_DATA_FILENAME failed\n");

	FILE* fp = fopen(filename, "r");
	free(filename);

	if(READ_MAPPING_INFO_FROM_FILES && fp != NULL){
		if(fread(inverse_mappings_manager[device_index].inverse_page_mapping_namespace_table, sizeof(uint32_t), devices[device_index].page_mapping_entry_nb, fp) <= 0)
			PERR("fread\n");
		fclose(fp);
	}
	else{
		uint64_t i;
		for(i=0; i<devices[device_index].page_mapping_entry_nb; i++){
			inverse_mappings_manager[device_index].inverse_page_mapping_namespace_table[i] = INVALID_NSID;
		}
	}
}

void INIT_INVERSE_BLOCK_MAPPING(uint8_t device_index)
{
	/* Allocation Memory for Inverse Block Mapping Table */
	inverse_mappings_manager[device_index].inverse_block_mapping_table_start = calloc(devices[device_index].block_mapping_entry_nb, sizeof(inverse_block_mapping_entry));
	if (inverse_mappings_manager[device_index].inverse_block_mapping_table_start == NULL)
		RERR(, "Calloc mapping table fail\n");

	/* Initialization Inverse Block Mapping Table */
	char* filename = GET_DATA_FILENAME(device_index, "inverse_block_mapping.dat");
	if (filename == NULL)
		RERR(, "GET_DATA_FILENAME failed\n");

	FILE* fp = fopen(filename, "r");
	free(filename);
	if(READ_MAPPING_INFO_FROM_FILES && fp != NULL){
		if(fread(inverse_mappings_manager[device_index].inverse_block_mapping_table_start, sizeof(inverse_block_mapping_entry), devices[device_index].block_mapping_entry_nb, fp) <= 0)
			PERR("fread\n");
		fclose(fp);
	}
	else{
		uint64_t i;
		inverse_block_mapping_entry* curr_mapping_entry = inverse_mappings_manager[device_index].inverse_block_mapping_table_start;

		for(i=0;i<devices[device_index].block_mapping_entry_nb;i++){
			curr_mapping_entry->type		= EMPTY_BLOCK;
			curr_mapping_entry->valid_page_nb	= 0;
			curr_mapping_entry->dirty_page_nb = 0;
			curr_mapping_entry->erase_count		= 0;
			curr_mapping_entry->victim = NULL;
			curr_mapping_entry += 1;
		}
	}
}

void INIT_VALID_ARRAY(uint8_t device_index)
{
	uint64_t i;
	inverse_block_mapping_entry* curr_mapping_entry = inverse_mappings_manager[device_index].inverse_block_mapping_table_start;
	char* valid_array;

	char* filename = GET_DATA_FILENAME(device_index, "valid_array.dat");
	if (filename == NULL)
		RERR(, "GET_DATA_FILENAME failed\n");

	FILE* fp = fopen(filename, "r");
	free(filename);
	if(READ_MAPPING_INFO_FROM_FILES && fp != NULL){
		for(i=0;i<devices[device_index].block_mapping_entry_nb;i++){
			valid_array = calloc(devices[device_index].page_nb, sizeof(char));
			if(fread(valid_array, sizeof(char), devices[device_index].page_nb, fp) <= 0)
				PERR("fread\n");
			curr_mapping_entry->valid_array = valid_array;

			curr_mapping_entry += 1;
		}
		fclose(fp);
	}
	else{
		for(i=0;i<devices[device_index].block_mapping_entry_nb;i++){
			valid_array = calloc(devices[device_index].page_nb, sizeof(char));
			memset(valid_array,0,devices[device_index].page_nb);
			curr_mapping_entry->valid_array = valid_array;

			curr_mapping_entry += 1;
		}
	}
}

void INIT_EMPTY_BLOCK_LIST(uint8_t device_index)
{
	uint64_t i, j, k;

	empty_block_entry* curr_entry;
	empty_block_root* curr_root;

	inverse_mappings_manager[device_index].empty_block_table_start = calloc(devices[device_index].planes_per_flash * devices[device_index].flash_nb, sizeof(empty_block_root));
	if (inverse_mappings_manager[device_index].empty_block_table_start == NULL)
		RERR(, "Calloc mapping table fail\n");

	char* filename = GET_DATA_FILENAME(device_index, "empty_block_list.dat");
	if (filename == NULL)
		RERR(, "GET_DATA_FILENAME failed\n");

	FILE* fp = fopen(filename, "r");
	free(filename);
	if(READ_MAPPING_INFO_FROM_FILES && fp != NULL){
		inverse_mappings_manager[device_index].total_empty_block_nb = 0;
		if(fread(inverse_mappings_manager[device_index].empty_block_table_start,sizeof(empty_block_root),devices[device_index].planes_per_flash*devices[device_index].flash_nb, fp) <= 0)
			PERR("fread\n");
		curr_root = inverse_mappings_manager[device_index].empty_block_table_start;

		for(i=0;i<devices[device_index].planes_per_flash;i++){

			for(j=0;j<devices[device_index].flash_nb;j++){

				inverse_mappings_manager[device_index].total_empty_block_nb += curr_root->empty_block_nb;
				k = curr_root->empty_block_nb;
				while(k > 0){
					curr_entry = calloc(1, sizeof(empty_block_entry));
					if(curr_entry == NULL){
						PERR("Calloc fail\n");
						break;
					}

					if(fread(curr_entry, sizeof(empty_block_entry), 1, fp) <= 0)
						PERR("fread\n");
					curr_entry->next = NULL;

					if(k == curr_root->empty_block_nb){
						curr_root->next = curr_entry;
						curr_root->tail = curr_entry;
					}
					else{
						curr_root->tail->next = curr_entry;
						curr_root->tail = curr_entry;
					}
					k--;
				}
				curr_root += 1;
			}
		}
		inverse_mappings_manager[device_index].empty_block_table_index = 0;
		fclose(fp);
	}
	else{
		curr_root = inverse_mappings_manager[device_index].empty_block_table_start;

		for(i=0;i<devices[device_index].planes_per_flash;i++){

			for(j=0;j<devices[device_index].flash_nb;j++){

				for(k=i; k < devices[device_index].block_nb; k+=devices[device_index].planes_per_flash){

					curr_entry = calloc(1, sizeof(empty_block_entry));
					if(curr_entry == NULL){
						PERR("Calloc fail\n");
						break;
					}

					if(k==i){
						curr_root->next = curr_entry;
						curr_root->tail = curr_entry;

						curr_root->tail->phy_flash_nb = j;
						curr_root->tail->phy_block_nb = k;
						curr_root->tail->curr_phy_page_nb = 0;
					}
					else{
						curr_root->tail->next = curr_entry;
						curr_root->tail = curr_entry;

						curr_root->tail->phy_flash_nb = j;
						curr_root->tail->phy_block_nb = k;
						curr_root->tail->curr_phy_page_nb = 0;
					}
					UPDATE_INVERSE_BLOCK_MAPPING(device_index, j, k, EMPTY_BLOCK);
				}
				curr_root->empty_block_nb = (unsigned int)devices[device_index].each_empty_table_entry_nb;
				curr_root += 1;
			}
		}
		inverse_mappings_manager[device_index].total_empty_block_nb = (int64_t)devices[device_index].block_mapping_entry_nb;
		inverse_mappings_manager[device_index].empty_block_table_index = 0;
	}
}

void INIT_VICTIM_BLOCK_LIST(uint8_t device_index)
{
	uint64_t i, j, k;

	victim_block_entry* curr_entry;
	victim_block_root* curr_root;

	inverse_mappings_manager[device_index].victim_block_table_start = calloc(devices[device_index].planes_per_flash * devices[device_index].flash_nb, sizeof(victim_block_root));
	if (inverse_mappings_manager[device_index].victim_block_table_start == NULL)
		RERR(, "Calloc mapping table fail\n");

	char* filename = GET_DATA_FILENAME(device_index, "victim_block_list.dat");
	if (filename == NULL)
		RERR(, "GET_DATA_FILENAME failed\n");

	FILE* fp = fopen(filename, "r");
	free(filename);
	if(READ_MAPPING_INFO_FROM_FILES && fp != NULL){
		inverse_mappings_manager[device_index].total_victim_block_nb = 0;
		if(fread(inverse_mappings_manager[device_index].victim_block_table_start, sizeof(victim_block_root), devices[device_index].planes_per_flash*devices[device_index].flash_nb, fp) <= 0)
			PERR("fread\n");
		curr_root = inverse_mappings_manager[device_index].victim_block_table_start;

		for(i=0;i<devices[device_index].planes_per_flash;i++){

			for(j=0;j<devices[device_index].flash_nb;j++){

				inverse_mappings_manager[device_index].total_victim_block_nb += curr_root->victim_block_nb;
				k = curr_root->victim_block_nb;
				while(k > 0){
					curr_entry = calloc(1, sizeof(victim_block_entry));
					if(curr_entry == NULL){
						PERR("Calloc fail\n");
						break;
					}

					if(fread(curr_entry, sizeof(victim_block_entry), 1, fp) <= 0)
						PERR("fread\n");
					curr_entry->next = NULL;
					curr_entry->prev = NULL;

					if(k == curr_root->victim_block_nb){
						curr_root->next = curr_entry;
						curr_root->tail = curr_entry;
					}
					else{
						curr_root->tail->next = curr_entry;
						curr_entry->prev = curr_root->tail;
						curr_root->tail = curr_entry;
					}
					k--;
				}
				curr_root += 1;
			}
		}
		fclose(fp);
	}
	else{
		curr_root = inverse_mappings_manager[device_index].victim_block_table_start;

		for(i=0;i<devices[device_index].planes_per_flash;i++){

			for(j=0;j<devices[device_index].flash_nb;j++){

				curr_root->next = NULL;
				curr_root->tail = NULL;
				curr_root->victim_block_nb = 0;

				curr_root += 1;
			}
		}
		inverse_mappings_manager[device_index].total_victim_block_nb = 0;
	}
}

void TERM_INVERSE_PAGE_MAPPING(uint8_t device_index)
{
	char* filename = GET_DATA_FILENAME(device_index, "inverse_page_mapping.dat");
	if (filename == NULL)
		RERR(, "GET_DATA_FILENAME failed\n");

	FILE* fp = fopen(filename, "w");
	free(filename);
	if (fp == NULL)
		RERR(, "File open fail\n");

	/* Write The inverse page table to file */
	if(fwrite(inverse_mappings_manager[device_index].inverse_page_mapping_table, sizeof(uint64_t), devices[device_index].page_mapping_entry_nb, fp) <= 0)
		PERR("fwrite\n");

	fclose(fp);

	/* Free the inverse page table memory */
	free(inverse_mappings_manager[device_index].inverse_page_mapping_table);
}

void TERM_INVERSE_PAGE_NAMESPACE_MAPPING(uint8_t device_index)
{
	char* filename = GET_DATA_FILENAME(device_index, "inverse_page_mapping_namespace.dat");
	if (filename == NULL)
		RERR(, "GET_DATA_FILENAME failed\n");

	FILE* fp = fopen(filename, "w");
	free(filename);

	if (fp == NULL)
		RERR(, "File open fail\n");

	/* Write The inverse page table to file */
	if(fwrite(inverse_mappings_manager[device_index].inverse_page_mapping_namespace_table, sizeof(uint32_t), devices[device_index].page_mapping_entry_nb, fp) <= 0)
		PERR("fwrite\n");

	fclose(fp);

	/* Free the inverse page table memory */
	free(inverse_mappings_manager[device_index].inverse_page_mapping_namespace_table);
}

void TERM_INVERSE_BLOCK_MAPPING(uint8_t device_index)
{
	char* filename = GET_DATA_FILENAME(device_index, "inverse_block_mapping.dat");
	if (filename == NULL)
		RERR(, "GET_DATA_FILENAME failed\n");

	FILE* fp = fopen(filename, "w");
	free(filename);
	if (fp == NULL)
		RERR(, "File open fail\n");

	/* Write The inverse block table to file */
	if(fwrite(inverse_mappings_manager[device_index].inverse_block_mapping_table_start, sizeof(inverse_block_mapping_entry), devices[device_index].block_mapping_entry_nb, fp) <= 0)
		PERR("fwrite\n");
	fclose(fp);

	/* Free The inverse block table memory */
	free(inverse_mappings_manager[device_index].inverse_block_mapping_table_start);
}

void TERM_VALID_ARRAY(uint8_t device_index)
{
	uint64_t i;
	inverse_block_mapping_entry* curr_mapping_entry = inverse_mappings_manager[device_index].inverse_block_mapping_table_start;
	char* valid_array;

	char* filename = GET_DATA_FILENAME(device_index, "valid_array.dat");
	if (filename == NULL)
		RERR(, "GET_DATA_FILENAME failed\n");

	FILE* fp = fopen(filename, "w");
	free(filename);
    if (fp == NULL)
		RERR(, "File open fail\n");

	for(i=0;i<devices[device_index].block_mapping_entry_nb;i++){
		valid_array = curr_mapping_entry->valid_array;
		if(fwrite(valid_array, sizeof(char), devices[device_index].page_nb, fp) <= 0)
			PERR("fwrite\n");
		curr_mapping_entry += 1;
		free(valid_array);
	}
	fclose(fp);
}

void TERM_EMPTY_BLOCK_LIST(uint8_t device_index)
{
	uint64_t i, j, k;

	empty_block_entry* curr_entry, *tmp_entry;
	empty_block_root* curr_root;

	char* filename = GET_DATA_FILENAME(device_index, "empty_block_list.dat");
	if (filename == NULL)
		RERR(, "GET_DATA_FILENAME failed\n");

	FILE* fp = fopen(filename, "w");
	free(filename);
	if (fp == NULL)
		RERR(, "File open fail\n");

	if(fwrite(inverse_mappings_manager[device_index].empty_block_table_start,sizeof(empty_block_root),devices[device_index].planes_per_flash*devices[device_index].flash_nb, fp) <= 0)
		PERR("fwrite\n");

	curr_root = inverse_mappings_manager[device_index].empty_block_table_start;
	for(i=0;i<devices[device_index].planes_per_flash;i++){

		for(j=0;j<devices[device_index].flash_nb;j++){

			k = curr_root->empty_block_nb;
			if(k != 0){
				curr_entry = curr_root->next;
			}
			while(k > 0){

				if(fwrite(curr_entry, sizeof(empty_block_entry), 1, fp) <= 0)
					PERR("fwrite\n");

				tmp_entry = curr_entry->next;
				free(curr_entry);
				curr_entry = tmp_entry;
				k--;
			}
			curr_root += 1;
		}
	}
	fclose(fp);
	free(inverse_mappings_manager[device_index].empty_block_table_start);
}

void TERM_VICTIM_BLOCK_LIST(uint8_t device_index)
{
	uint64_t i, j, k;

	victim_block_entry* curr_entry, *tmp_entry;
	victim_block_root* curr_root;

	char* filename = GET_DATA_FILENAME(device_index, "victim_block_list.dat");
	if (filename == NULL)
		RERR(, "GET_DATA_FILENAME failed\n");

	FILE* fp = fopen(filename, "w");
	free(filename);
	if (fp == NULL)
		RERR(, "File open fail\n");

	if(fwrite(inverse_mappings_manager[device_index].victim_block_table_start, sizeof(victim_block_root), devices[device_index].planes_per_flash*devices[device_index].flash_nb, fp) <= 0)
		PERR("fwrite\n");

	curr_root = inverse_mappings_manager[device_index].victim_block_table_start;
	for(i=0;i<devices[device_index].planes_per_flash;i++){

		for(j=0;j<devices[device_index].flash_nb;j++){

			k = curr_root->victim_block_nb;
			if(k != 0){
				curr_entry = curr_root->next;
			}
			while(k > 0){

				if(fwrite(curr_entry, sizeof(victim_block_entry), 1, fp) <= 0)
					PERR("fwrite\n");

				tmp_entry = curr_entry->next;
				free(curr_entry);
				curr_entry = tmp_entry;
				k--;
			}
			curr_root += 1;
		}
	}
	fclose(fp);
	free(inverse_mappings_manager[device_index].victim_block_table_start);
}

//If we're using the VICTIM_OVERALL option, then a candidate block
// (one with an empty page available) is returned from a different
// flash plane each time, sequentially (wraps at the end and starts
// all over again)
empty_block_entry* GET_EMPTY_BLOCK(uint8_t device_index, int mode, uint64_t mapping_index)
{
	if (inverse_mappings_manager[device_index].total_empty_block_nb == 0)
		RERR(NULL, "There is no empty block\n");

	uint64_t input_mapping_index = mapping_index;

	empty_block_entry* curr_empty_block;
	empty_block_root* curr_root_entry;

	while(inverse_mappings_manager[device_index].total_empty_block_nb != 0){

		if (mode == VICTIM_OVERALL){
			curr_root_entry = inverse_mappings_manager[device_index].empty_block_table_start + inverse_mappings_manager[device_index].empty_block_table_index;

			if(curr_root_entry->empty_block_nb == 0){
				inverse_mappings_manager[device_index].empty_block_table_index++;
				if(inverse_mappings_manager[device_index].empty_block_table_index == devices[device_index].empty_table_entry_nb){
					inverse_mappings_manager[device_index].empty_block_table_index = 0;
				}
				continue;
			}
			else
			{
				curr_empty_block = curr_root_entry->next;
				if(curr_empty_block->curr_phy_page_nb == devices[device_index].page_nb){

					/* Update Empty Block List */
					if(curr_root_entry->empty_block_nb == 1){
						curr_root_entry->next = NULL;
						curr_root_entry->empty_block_nb = 0;
					}
					else{
						curr_root_entry->next = curr_empty_block->next;
						curr_root_entry->empty_block_nb -= 1;
					}

					/* Eject Empty Block from the list */
					INSERT_VICTIM_BLOCK(device_index, curr_empty_block);

					/* Update The total number of empty block */
					inverse_mappings_manager[device_index].total_empty_block_nb--;

					inverse_mappings_manager[device_index].empty_block_table_index++;
					if(inverse_mappings_manager[device_index].empty_block_table_index == devices[device_index].empty_table_entry_nb){
						inverse_mappings_manager[device_index].empty_block_table_index = 0;
					}
					continue;
				}
				inverse_mappings_manager[device_index].empty_block_table_index++;
				if(inverse_mappings_manager[device_index].empty_block_table_index == devices[device_index].empty_table_entry_nb){
					inverse_mappings_manager[device_index].empty_block_table_index = 0;
				}
				return curr_empty_block;
			}
		}
		else if(mode == VICTIM_INCHIP){
			curr_root_entry = inverse_mappings_manager[device_index].empty_block_table_start + mapping_index;
			if(curr_root_entry->empty_block_nb == 0){

				mapping_index++;
				if(mapping_index % devices[device_index].planes_per_flash == 0){
					mapping_index = mapping_index - devices[device_index].planes_per_flash;
				}
				if (mapping_index == input_mapping_index)
					RINFO(NULL, "There is no empty block\n");

				continue;
			}
			else{
				curr_empty_block = curr_root_entry->next;
				if(curr_empty_block->curr_phy_page_nb == devices[device_index].page_nb){

					/* Update Empty Block List */
					if(curr_root_entry->empty_block_nb == 1){
						curr_root_entry->next = NULL;
						curr_root_entry->empty_block_nb = 0;
					}
					else{
						curr_root_entry->next = curr_empty_block->next;
						curr_root_entry->empty_block_nb -= 1;
					}

					/* Eject Empty Block from the list */
					INSERT_VICTIM_BLOCK(device_index, curr_empty_block);

					/* Update The total number of empty block */
					inverse_mappings_manager[device_index].total_empty_block_nb--;

					continue;
				}
				else{
					curr_empty_block = curr_root_entry->next;
				}

				return curr_empty_block;
			}
		}

		else if(mode == VICTIM_NOPARAL){
			//Seems to have a bug here, not used somewhere in the project at the moment.
			curr_root_entry = inverse_mappings_manager[device_index].empty_block_table_start + mapping_index;
			if(curr_root_entry->empty_block_nb == 0){

				mapping_index++;
				inverse_mappings_manager[device_index].empty_block_table_index++;
				if (mapping_index == devices[device_index].empty_table_entry_nb) {
					mapping_index = 0;
					inverse_mappings_manager[device_index].empty_block_table_index = 0;
				}
				continue;
			}
			else{
				curr_empty_block = curr_root_entry->next;
				if(curr_empty_block->curr_phy_page_nb == devices[device_index].page_nb){

					/* Update Empty Block List */
					if(curr_root_entry->empty_block_nb == 1){
						curr_root_entry->next = NULL;
						curr_root_entry->empty_block_nb = 0;
					}
					else{
						curr_root_entry->next = curr_empty_block->next;
						curr_root_entry->empty_block_nb -= 1;
					}

					/* Eject Empty Block from the list */
					INSERT_VICTIM_BLOCK(device_index, curr_empty_block);

					/* Update The total number of empty block */
					inverse_mappings_manager[device_index].total_empty_block_nb--;

					continue;
				}
				else{
					curr_empty_block = curr_root_entry->next;
				}

				return curr_empty_block;
			}
		}
	}

	RERR(NULL, "There is no empty block\n");
}

ftl_ret_val INSERT_EMPTY_BLOCK(uint8_t device_index, unsigned int phy_flash_nb, uint64_t phy_block_nb)
{
	uint64_t mapping_index;
	int plane_nb;

	empty_block_root* curr_root_entry;
	empty_block_entry* new_empty_block;

	new_empty_block = calloc(1, sizeof(empty_block_entry));
	if (new_empty_block == NULL)

		RERR(FTL_FAILURE, "Alloc new empty block fail\n");


	/* Init New empty block */
	new_empty_block->phy_flash_nb = phy_flash_nb;
	new_empty_block->phy_block_nb = phy_block_nb;
	new_empty_block->next = NULL;

	plane_nb = phy_block_nb % devices[device_index].planes_per_flash;
	mapping_index = plane_nb * devices[device_index].flash_nb + phy_flash_nb;

	curr_root_entry = inverse_mappings_manager[device_index].empty_block_table_start + mapping_index;

	if(curr_root_entry->empty_block_nb == 0){
		curr_root_entry->next = new_empty_block;
		curr_root_entry->tail = new_empty_block;
		curr_root_entry->empty_block_nb = 1;
	}
	else{
		curr_root_entry->tail->next = new_empty_block;
		curr_root_entry->tail = new_empty_block;
		curr_root_entry->empty_block_nb++;
	}
	inverse_mappings_manager[device_index].total_empty_block_nb++;

	return FTL_SUCCESS;
}

ftl_ret_val INSERT_VICTIM_BLOCK(uint8_t device_index, empty_block_entry* full_block){

	uint64_t mapping_index;
	int plane_nb;

	victim_block_root* curr_root_entry;
	victim_block_entry* new_victim_block;
	victim_block_entry* curr_min_victim_block;

	inverse_block_mapping_entry* inverse_block_entry;
	inverse_block_entry = GET_INVERSE_BLOCK_MAPPING_ENTRY(device_index, full_block->phy_flash_nb, full_block->phy_block_nb);

	/* Alloc New victim block entry */
	new_victim_block = calloc(1, sizeof(victim_block_entry));
	if (new_victim_block == NULL) {
		RERR(FTL_FAILURE, "Calloc fail\n");
	}

	inverse_block_entry->victim = new_victim_block;

	/* Copy the full block address */
	new_victim_block->phy_flash_nb = full_block->phy_flash_nb;
	new_victim_block->phy_block_nb = full_block->phy_block_nb;
	new_victim_block->valid_page_nb = &(inverse_block_entry->valid_page_nb);
	new_victim_block->prev = NULL;
	new_victim_block->next = NULL;

	plane_nb = full_block->phy_block_nb % devices[device_index].planes_per_flash;
	mapping_index = plane_nb * devices[device_index].flash_nb + full_block->phy_flash_nb;

	curr_root_entry = inverse_mappings_manager[device_index].victim_block_table_start + mapping_index;

	/* Update victim block list */
	if(curr_root_entry->victim_block_nb == 0){
		curr_root_entry->next = new_victim_block;
		curr_root_entry->tail = new_victim_block;
		curr_root_entry->victim_block_nb = 1;
	}
	else{
		//search for node with at least the same number of valid_page_nb
		curr_min_victim_block = curr_root_entry->next;

		//if first valid_page_nb is larger, insert before
		if(*(curr_min_victim_block->valid_page_nb) >= *(new_victim_block->valid_page_nb)){
			curr_min_victim_block->prev = new_victim_block;
			new_victim_block->next = curr_min_victim_block;

			curr_root_entry->next = new_victim_block;
		}else{//else, go over list and insert after
			//find block to insert after
			while(curr_min_victim_block->next != NULL && *(curr_min_victim_block->next->valid_page_nb) < *(new_victim_block->valid_page_nb)){
				curr_min_victim_block = curr_min_victim_block->next;
			}

			//insert new_victim_block after curr_min_victim_block
			new_victim_block->prev = curr_min_victim_block;
			new_victim_block->next = curr_min_victim_block->next;

			if(curr_min_victim_block->next != NULL){
				curr_min_victim_block->next->prev = new_victim_block;
			}
			curr_min_victim_block->next = new_victim_block;

			if(new_victim_block->next == NULL){//if last in list
				curr_root_entry->tail = new_victim_block;
			}

		}

		curr_root_entry->victim_block_nb++;
	}

	/* Free the full empty block entry */
	free(full_block);

	/* Update the total number of victim block */
	inverse_mappings_manager[device_index].total_victim_block_nb++;

	return FTL_SUCCESS;
}

int EJECT_VICTIM_BLOCK(uint8_t device_index, victim_block_entry* victim_block){

	uint64_t mapping_index;
	int plane_nb;

	victim_block_root* curr_root_entry;

	plane_nb = victim_block->phy_block_nb % devices[device_index].planes_per_flash;
	mapping_index = plane_nb * devices[device_index].flash_nb + victim_block->phy_flash_nb;

	inverse_block_mapping_entry* inverse_block_entry;
	inverse_block_entry = GET_INVERSE_BLOCK_MAPPING_ENTRY(device_index, victim_block->phy_flash_nb, victim_block->phy_block_nb);
	inverse_block_entry->victim = NULL;

	curr_root_entry = inverse_mappings_manager[device_index].victim_block_table_start + mapping_index;

	/* Update victim block list */
	if(victim_block == curr_root_entry->next){
		if(curr_root_entry->victim_block_nb == 1){
			curr_root_entry->next = NULL;
			curr_root_entry->tail = NULL;
		}
		else{
			curr_root_entry->next = victim_block->next;
			curr_root_entry->next->prev = NULL;
		}
	}
	else if(victim_block == curr_root_entry->tail){
		curr_root_entry->tail = victim_block->prev;
		curr_root_entry->tail->next = NULL;
	}
	else{
		victim_block->prev->next = victim_block->next;
		victim_block->next->prev = victim_block->prev;
	}

	curr_root_entry->victim_block_nb--;
	inverse_mappings_manager[device_index].total_victim_block_nb--;

	/* Free the victim block */
	free(victim_block);

	return FTL_SUCCESS;
}

inverse_block_mapping_entry* GET_INVERSE_BLOCK_MAPPING_ENTRY(uint8_t device_index, unsigned int phy_flash_nb, uint64_t phy_block_nb){

	uint64_t mapping_index = phy_flash_nb * devices[device_index].block_nb + phy_block_nb;

	inverse_block_mapping_entry* mapping_entry = inverse_mappings_manager[device_index].inverse_block_mapping_table_start + mapping_index;

	return mapping_entry;
}

void GET_INVERSE_MAPPING_INFO(uint8_t device_index, uint64_t ppn, uint32_t *o_nsid, uint64_t *o_lpn)
{
	if(ppn >= devices[device_index].page_mapping_entry_nb) {
		PERR("overflow!\n");
	}

	*o_lpn = inverse_mappings_manager[device_index].inverse_page_mapping_table[ppn];
	*o_nsid = inverse_mappings_manager[device_index].inverse_page_mapping_namespace_table[ppn];
}

int UPDATE_INVERSE_PAGE_MAPPING(uint8_t device_index, uint64_t ppn, uint32_t nsid, uint64_t lpn)
{
	if(nsid >= MAX_NUMBER_OF_NAMESPACES && nsid != INVALID_NSID){
		PERR("overflow!\n");
	}
	
	inverse_mappings_manager[device_index].inverse_page_mapping_table[ppn] = lpn;
	inverse_mappings_manager[device_index].inverse_page_mapping_namespace_table[ppn] = nsid;

	return FTL_SUCCESS;
}

int UPDATE_INVERSE_BLOCK_MAPPING(uint8_t device_index, unsigned int phy_flash_nb, uint64_t phy_block_nb, int type)
{
        uint64_t i;
        inverse_block_mapping_entry* mapping_entry = GET_INVERSE_BLOCK_MAPPING_ENTRY(device_index, phy_flash_nb, phy_block_nb);

        mapping_entry->type = type;

        if(type == EMPTY_BLOCK){
                for(i=0;i<devices[device_index].page_nb;i++){
                        UPDATE_INVERSE_BLOCK_VALIDITY(device_index, phy_flash_nb, phy_block_nb, i, PAGE_ZERO);
                }
        }

        return FTL_SUCCESS;
}

void UPDATE_VICTIM_LIST(uint8_t device_index, victim_block_entry *victim_entry){
	if(victim_entry == NULL){
		return;
	}
	uint32_t plane_nb = victim_entry->phy_block_nb % devices[device_index].planes_per_flash;
	uint64_t mapping_index = plane_nb * devices[device_index].flash_nb + victim_entry->phy_flash_nb;

	victim_block_root* curr_root_entry = inverse_mappings_manager[device_index].victim_block_table_start + mapping_index;

	victim_block_entry *curr_victim_entry;
	//if victim_entry.valid_page_nb > victim_entry->next->valid_page_nb then need to go foward and find correct spot
	if(victim_entry->next != NULL && *(victim_entry->valid_page_nb) > *(victim_entry->next->valid_page_nb)){
		curr_victim_entry = victim_entry->next;

		//remove victim_entry from list
		if(victim_entry->prev != NULL){
			victim_entry->prev->next = victim_entry->next;
		}
		if(victim_entry->next != NULL){
			victim_entry->next->prev = victim_entry->prev;
		}
		if(curr_root_entry->next == victim_entry){
			curr_root_entry->next = victim_entry->next;
		}
		if(curr_root_entry->tail == victim_entry){
			curr_root_entry->tail = victim_entry->prev;
		}

		victim_entry->next = NULL;
		victim_entry->prev = NULL;

		//find block to insert after
		while(curr_victim_entry->next != NULL && *(curr_victim_entry->next->valid_page_nb) < *(victim_entry->valid_page_nb)){
			curr_victim_entry = curr_victim_entry->next;
		}

		//insert victim_entry after curr_victim_entry
		victim_entry->prev = curr_victim_entry;
		victim_entry->next = curr_victim_entry->next;

		if(curr_victim_entry->next){
			curr_victim_entry->next->prev = victim_entry;
		}
		curr_victim_entry->next = victim_entry;

		if(victim_entry->next == NULL){//if last in list
			curr_root_entry->tail = victim_entry;
		}

	}else if(victim_entry->prev != NULL && *(victim_entry->valid_page_nb) < *(victim_entry->prev->valid_page_nb)){//if victim_entry.valid_page_nb > victim_entry->prev->valid_page_nb then need to go back and find correct spot
		curr_victim_entry = victim_entry->prev;

		//remove victim_entry from list
		if(victim_entry->prev != NULL){
			victim_entry->prev->next = victim_entry->next;
		}
		if(victim_entry->next != NULL){
			victim_entry->next->prev = victim_entry->prev;
		}
		if(curr_root_entry->next == victim_entry){
			curr_root_entry->next = victim_entry->next;
		}
		if(curr_root_entry->tail == victim_entry){
			curr_root_entry->tail = victim_entry->prev;
		}

		victim_entry->next = NULL;
		victim_entry->prev = NULL;

		//find block to insert after
		while(curr_victim_entry->prev != NULL && *(curr_victim_entry->prev->valid_page_nb) > *(victim_entry->valid_page_nb)){
			curr_victim_entry = curr_victim_entry->prev;
		}

		//insert victim_entry after curr_victim_entry
		victim_entry->next = curr_victim_entry;//
		victim_entry->prev = curr_victim_entry->prev;

		if(curr_victim_entry->prev != NULL){
			curr_victim_entry->prev->next = victim_entry;
		}
		curr_victim_entry->prev = victim_entry;

		if(victim_entry->prev == NULL){//if last in list
			curr_root_entry->next = victim_entry;
		}

	}
	//otherwise do nothing
}

ftl_ret_val UPDATE_INVERSE_BLOCK_VALIDITY(uint8_t device_index, unsigned int phy_flash_nb, uint64_t phy_block_nb, uint64_t phy_page_nb, char valid)
{
	if (phy_flash_nb >= devices[device_index].flash_nb || phy_block_nb >= devices[device_index].block_nb || phy_page_nb >= devices[device_index].page_nb)
		RERR(FTL_FAILURE, "Wrong physical address\n");

	inverse_block_mapping_entry *mapping_entry =
		GET_INVERSE_BLOCK_MAPPING_ENTRY(device_index, phy_flash_nb, phy_block_nb);

	victim_block_entry *victim_entry = mapping_entry->victim;
	char old_state = mapping_entry->valid_array[phy_page_nb];
	if(old_state == PAGE_INVALID && valid == PAGE_VALID){
		RERR(FTL_FAILURE, "Writing to a page that was already written to without erase.\n");
	}
	if ((valid == PAGE_VALID) && (old_state != PAGE_VALID)){
		mapping_entry->valid_page_nb++;
		UPDATE_VICTIM_LIST(device_index, victim_entry);
	}
	else if ((valid != PAGE_VALID) && (old_state == PAGE_VALID)){
		mapping_entry->valid_page_nb--;
		UPDATE_VICTIM_LIST(device_index, victim_entry);
	}
	mapping_entry->valid_array[phy_page_nb] = valid;

	return FTL_SUCCESS;
}
