// Copyright(c)2013 
//
// Hanyang University, Seoul, Korea
// Embedded Software Systems Lab. All right reserved

#include "common.h"

uint32_t *inverse_page_mapping_table;
void *inverse_block_mapping_table_start;

void *empty_block_table_start;
void *victim_block_table_start;

int64_t total_empty_block_nb;
int64_t total_victim_block_nb;

unsigned int empty_block_table_index;

void INIT_INVERSE_PAGE_MAPPING(void)
{
	/* Allocation Memory for Inverse Page Mapping Table */
	inverse_page_mapping_table = (void*)calloc(PAGE_MAPPING_ENTRY_NB, sizeof(uint32_t));
	if (inverse_page_mapping_table == NULL)
		RERR(, "Calloc mapping table fail\n");

	/* Initialization Inverse Page Mapping Table */
	FILE *fp = fopen("./data/inverse_page_mapping.dat", "r");
	if (fp != NULL){
		if (fread(inverse_page_mapping_table, sizeof(uint32_t), PAGE_MAPPING_ENTRY_NB, fp) <= 0)
			PERR("fread\n");
		fclose(fp);
	}
	else{
		int i;
		for(i = 0; i < PAGE_MAPPING_ENTRY_NB; i++)
			inverse_page_mapping_table[i] = -1;
	}
}

void INIT_INVERSE_BLOCK_MAPPING(void)
{
	/* Allocation Memory for Inverse Block Mapping Table */
	inverse_block_mapping_table_start = calloc(BLOCK_MAPPING_ENTRY_NB, sizeof(inverse_block_mapping_entry));
	if (inverse_block_mapping_table_start == NULL)
		RERR(, "Calloc mapping table fail\n");

	/* Initialization Inverse Block Mapping Table */
	FILE *fp = fopen("./data/inverse_block_mapping.dat", "r");
	if (fp != NULL){
		if (fread(inverse_block_mapping_table_start, sizeof(inverse_block_mapping_entry), BLOCK_MAPPING_ENTRY_NB, fp) <= 0)
			PERR("fread\n");
		fclose(fp);
	}
	else{
		int i;
		inverse_block_mapping_entry *curr_mapping_entry = (inverse_block_mapping_entry*)inverse_block_mapping_table_start;

		for(i = 0; i < BLOCK_MAPPING_ENTRY_NB; i++){
			curr_mapping_entry->type = EMPTY_BLOCK;
			curr_mapping_entry->valid_page_nb = 0;
			curr_mapping_entry->erase_count = 0;
			curr_mapping_entry += 1;
		}
	}
}

void INIT_VALID_ARRAY(void)
{
	int i;
	inverse_block_mapping_entry *curr_mapping_entry = (inverse_block_mapping_entry*)inverse_block_mapping_table_start;
	char *valid_array;

	FILE *fp = fopen("./data/valid_array.dat", "r");
	if (fp != NULL){
		for (i = 0; i < BLOCK_MAPPING_ENTRY_NB; i++){
			valid_array = (char*)calloc(PAGE_NB, sizeof(char));
			if (fread(valid_array, sizeof(char), PAGE_NB, fp) <= 0)
				PERR("fread\n");
			curr_mapping_entry->valid_array = valid_array;
			curr_mapping_entry += 1;
		}
		fclose(fp);
	}
	else{
		for (i = 0; i < BLOCK_MAPPING_ENTRY_NB; i++){
			valid_array = (char*)calloc(PAGE_NB, sizeof(char));
			memset(valid_array,0,PAGE_NB);
			curr_mapping_entry->valid_array = valid_array;
			curr_mapping_entry += 1;
		}			
	}
}

void INIT_EMPTY_BLOCK_LIST(void)
{
	int i, j, k;
	empty_block_entry *curr_entry;
	empty_block_root *curr_root;

	empty_block_table_start = calloc(PLANES_NB, sizeof(empty_block_root));
	if (empty_block_table_start == NULL)
		RERR(, "calloc mapping table fail\n");

	FILE *fp = fopen("./data/empty_block_list.dat", "r");
	if (fp != NULL){
		total_empty_block_nb = 0;
		if (fread(empty_block_table_start, sizeof(empty_block_root), PLANES_NB, fp) <= 0)
			PERR("fread\n");
		curr_root = (empty_block_root*)empty_block_table_start;

		for (i = 0; i < PLANES_PER_FLASH; i++){
			for (j = 0; j < FLASH_NB; j++, curr_root += 1){
				total_empty_block_nb += curr_root->empty_block_nb;
				for (k = curr_root->empty_block_nb; k > 0; k--){
					curr_entry = (empty_block_entry*)malloc(sizeof(empty_block_entry));
					if (curr_entry == NULL){
						PERR("malloc fail\n");
						break;
					}

					if(fread(curr_entry, sizeof(empty_block_entry), 1, fp) <= 0)
						PERR("fread\n");
					curr_entry->next = NULL;

					if(k == curr_root->empty_block_nb)
						curr_root->tail = curr_root->next = curr_entry;
					else
						curr_root->tail = curr_root->tail->next = curr_entry;
				}
			}
		}
		fclose(fp);
	}
	else{
		curr_root = (empty_block_root*)empty_block_table_start;
		for (i = 0; i < PLANES_PER_FLASH; i++){
			for (j = 0; j < FLASH_NB; j++, curr_root += 1){
				for (k = i; k < BLOCK_NB; k += PLANES_PER_FLASH){
					curr_entry = (empty_block_entry*)malloc(sizeof(empty_block_entry));	
					if (curr_entry == NULL){
						PERR("malloc fail\n");
						break;
					}

					if(k == i)
						curr_root->tail = curr_root->next = curr_entry;
					else
						curr_root->tail = curr_root->tail->next = curr_entry;
					curr_root->tail->phy_flash_nb = j;
					curr_root->tail->phy_block_nb = k;
					curr_root->tail->curr_phy_page_nb = 0;
					UPDATE_INVERSE_BLOCK_MAPPING(j, k, EMPTY_BLOCK);
				}
				curr_root->empty_block_nb = (unsigned int)EACH_EMPTY_TABLE_ENTRY_NB;
			}
		}
		total_empty_block_nb = (int64_t)BLOCK_MAPPING_ENTRY_NB;
	}
	empty_block_table_index = 0;
}

void INIT_VICTIM_BLOCK_LIST(void)
{
	int i, j, k;
	victim_block_entry *curr_entry;
	victim_block_root *curr_root;

	victim_block_table_start = calloc(PLANES_NB, sizeof(victim_block_root));
	if (victim_block_table_start == NULL)
		RERR(, "calloc mapping table fail\n");

	FILE *fp = fopen("./data/victim_block_list.dat", "r");
	if (fp != NULL){
		total_victim_block_nb = 0;
		if (fread(victim_block_table_start, sizeof(victim_block_root), PLANES_NB, fp) <= 0)
			PERR("fread\n");
		curr_root = (victim_block_root*)victim_block_table_start;

		for (i = 0; i < PLANES_PER_FLASH; i++){
			for (j = 0; j < FLASH_NB; j++, curr_root += 1){
				total_victim_block_nb += curr_root->victim_block_nb;
				for(k = curr_root->victim_block_nb; k > 0; k--){
					curr_entry = (victim_block_entry*)malloc(sizeof(victim_block_entry));
					if(curr_entry == NULL){
						PERR("malloc fail\n");
						break;
					}

					if(fread(curr_entry, sizeof(victim_block_entry), 1, fp) <= 0)
						PERR("fread\n");
					curr_entry->prev = curr_entry->next = NULL;

					if(k == curr_root->victim_block_nb)
						curr_root->tail = curr_root->next = curr_entry;
					else{
						curr_entry->prev = curr_root->tail;
						curr_root->tail = curr_root->tail->next = curr_entry;
					}
				}
			}
		}
		fclose(fp);
	}
	else{
		curr_root = (victim_block_root*)victim_block_table_start;		
		for (i = 0; i < PLANES_PER_FLASH; i++){
			for (j = 0; j < FLASH_NB; j++, curr_root += 1){
				curr_root->tail = curr_root->next = NULL;
				curr_root->victim_block_nb = 0;
			}
		}
		total_victim_block_nb = 0;
	}
}

void TERM_INVERSE_PAGE_MAPPING(void)
{
	FILE *fp = fopen("./data/inverse_page_mapping.dat", "w");
	if (fp == NULL)
		RERR(, "File open fail\n");

	if (fwrite(inverse_page_mapping_table, sizeof(uint32_t), PAGE_MAPPING_ENTRY_NB, fp) <= 0)
		PERR("fwrite\n");
	fclose(fp);
	free(inverse_page_mapping_table);
}

void TERM_INVERSE_BLOCK_MAPPING(void)
{
	FILE *fp = fopen("./data/inverse_block_mapping.dat", "w");
	if (fp == NULL)
		RERR(, "File open fail\n");

	if (fwrite(inverse_block_mapping_table_start, sizeof(inverse_block_mapping_entry), BLOCK_MAPPING_ENTRY_NB, fp) <= 0)
		PERR("fwrite\n");
	fclose(fp);
	free(inverse_block_mapping_table_start);
}

void TERM_VALID_ARRAY(void)
{
	int i;
	inverse_block_mapping_entry *curr_mapping_entry = (inverse_block_mapping_entry*)inverse_block_mapping_table_start;
	char *valid_array;

	FILE *fp = fopen("./data/valid_array.dat", "w");
	if (fp == NULL)
		RERR(, "File open fail\n");

	for (i = 0; i < BLOCK_MAPPING_ENTRY_NB; i++, curr_mapping_entry += 1){
		valid_array = curr_mapping_entry->valid_array;
		if (fwrite(valid_array, sizeof(char), PAGE_NB, fp) <= 0)
			PERR("fwrite\n");
		free(valid_array); /*MYCODE*/
	}
	fclose(fp);
}

void TERM_EMPTY_BLOCK_LIST(void)
{
	int i, j, k;
	empty_block_entry *curr_entry, *tmp_entry;
	empty_block_root *curr_root;

	FILE *fp = fopen("./data/empty_block_list.dat", "w");
	if (fp == NULL)
		RERR(, "File open fail\n");

	if (fwrite(empty_block_table_start,sizeof(empty_block_root),PLANES_NB, fp) <= 0)
		PERR("fwrite\n");

	curr_root = (empty_block_root*)empty_block_table_start;
	for (i = 0; i < PLANES_PER_FLASH; i++){
		for (j = 0; j < FLASH_NB; j++, curr_root += 1){
			k = curr_root->empty_block_nb;
			if (k != 0)
				curr_entry = (empty_block_entry*)curr_root->next;
			for (; k > 0; k--){
				if (fwrite(curr_entry, sizeof(empty_block_entry), 1, fp) <= 0)
					PERR("fwrite\n");
				tmp_entry = curr_entry->next; /*MYCODE*/
				free(curr_entry);
				curr_entry = tmp_entry;
			}
		}
	}
	fclose(fp);
	free(empty_block_table_start);
}

void TERM_VICTIM_BLOCK_LIST(void)
{
	int i, j, k;
	victim_block_entry *curr_entry, *tmp_entry;
	victim_block_root *curr_root;

	FILE *fp = fopen("./data/victim_block_list.dat", "w");
	if (fp == NULL)
		RERR(, "File open fail\n");

	if (fwrite(victim_block_table_start, sizeof(victim_block_root), PLANES_NB, fp) <= 0)
		PERR("fwrite\n");

	curr_root = (victim_block_root*)victim_block_table_start;
	for (i = 0; i < PLANES_PER_FLASH; i++){
		for (j = 0; j < FLASH_NB; j++, curr_root += 1){
			k = curr_root->victim_block_nb;
			if (k != 0)
				curr_entry = (victim_block_entry*)curr_root->next;
			for (; k > 0; k--){
				if (fwrite(curr_entry, sizeof(victim_block_entry), 1, fp) <= 0)
					PERR("fwrite\n");
				tmp_entry = curr_entry->next; /*MYCODE*/
				free(curr_entry);
				curr_entry = tmp_entry;
			}
		}
	}
	fclose(fp);
	free(victim_block_table_start);
}

empty_block_entry *GET_EMPTY_BLOCK(int mode, int mapping_index)
{
	if(total_empty_block_nb == 0)
		RERR(NULL, "There is no empty block\n");

	int input_mapping_index = mapping_index;
	empty_block_entry *curr_empty_block;
	empty_block_root *curr_root_entry;

	while (total_empty_block_nb != 0){
		if (mode == VICTIM_OVERALL){
			curr_root_entry = (empty_block_root*)empty_block_table_start + empty_block_table_index;
			if (curr_root_entry->empty_block_nb == 0){
				empty_block_table_index++;
				if (empty_block_table_index == EMPTY_TABLE_ENTRY_NB)
					empty_block_table_index = 0;
				continue;
			}
			else{
				curr_empty_block = curr_root_entry->next;
				if (curr_empty_block->curr_phy_page_nb == PAGE_NB){
					/* Update Empty Block List */
					if (curr_root_entry->empty_block_nb == 1){
						curr_root_entry->next = NULL;
						curr_root_entry->empty_block_nb = 0;
					}
					else{
						curr_root_entry->next = curr_empty_block->next;
						curr_root_entry->empty_block_nb -= 1;
					}

					/* Eject Empty Block from the list */
					INSERT_VICTIM_BLOCK(curr_empty_block);

					/* Update The total number of empty block */
					total_empty_block_nb--;

					empty_block_table_index++;
					if (empty_block_table_index == EMPTY_TABLE_ENTRY_NB)
						empty_block_table_index = 0;
					continue;
				}
				empty_block_table_index++;
				if (empty_block_table_index == EMPTY_TABLE_ENTRY_NB)
					empty_block_table_index = 0;
				return curr_empty_block;
			}
		}
		else if (mode == VICTIM_INCHIP){
			curr_root_entry = (empty_block_root*)empty_block_table_start + mapping_index;
			if (curr_root_entry->empty_block_nb == 0){
				mapping_index++;
				if (mapping_index % PLANES_PER_FLASH == 0)
					mapping_index = mapping_index - (PLANES_PER_FLASH-1);
				if (mapping_index == input_mapping_index)
					RERR(NULL, "There is no empty block\n");
				continue;
			}
			else{
				curr_empty_block = curr_root_entry->next;
				if (curr_empty_block == NULL)
					RERR(NULL, "MYERR1\n");
				if (curr_empty_block->curr_phy_page_nb == PAGE_NB){
					/* Update Empty Block List */
					if (curr_root_entry->empty_block_nb == 1){
						curr_root_entry->next = NULL;
						curr_root_entry->empty_block_nb = 0;
					}
					else{
						curr_root_entry->next = curr_empty_block->next;
						curr_root_entry->empty_block_nb -= 1;
					}

					/* Eject Empty Block from the list */
					INSERT_VICTIM_BLOCK(curr_empty_block);

					/* Update The total number of empty block */
					total_empty_block_nb--;

					continue;
				}
				else
					curr_empty_block = curr_root_entry->next;
				return curr_empty_block;
			}
		}
		else if (mode == VICTIM_NOPARAL){
			//Seems to have a bug here, not used somewhere in the project at the moment.
			curr_root_entry = (empty_block_root*)empty_block_table_start + mapping_index;
			if (curr_root_entry->empty_block_nb == 0){
				mapping_index++;
				empty_block_table_index++;
				if (mapping_index == EMPTY_TABLE_ENTRY_NB)
					empty_block_table_index = mapping_index = 0;
				continue;
			}
			else{
				curr_empty_block = curr_root_entry->next;
				if (curr_empty_block->curr_phy_page_nb == PAGE_NB){
					/* Update Empty Block List */
					if (curr_root_entry->empty_block_nb == 1){
						curr_root_entry->next = NULL;
						curr_root_entry->empty_block_nb = 0;
					}
					else{
						curr_root_entry->next = curr_empty_block->next;
						curr_root_entry->empty_block_nb -= 1;
					}

					/* Eject Empty Block from the list */
					INSERT_VICTIM_BLOCK(curr_empty_block);

					/* Update The total number of empty block */
					total_empty_block_nb--;

					continue;
				}
				else
					curr_empty_block = curr_root_entry->next;
				return curr_empty_block;
			}	
		}
	}
	RERR(NULL, "There is no empty block\n");
}

int INSERT_EMPTY_BLOCK(unsigned int phy_flash_nb, unsigned int phy_block_nb)
{
	int mapping_index;
	int plane_nb;

	empty_block_root *curr_root_entry;
	empty_block_entry *new_empty_block;

	new_empty_block = (empty_block_entry*)malloc(sizeof(empty_block_entry));
	if (new_empty_block == NULL)
		RERR(FAIL, "mlloc new empty block fail\n");

	/* Init New empty block */
	new_empty_block->phy_flash_nb = phy_flash_nb;
	new_empty_block->phy_block_nb = phy_block_nb;
	new_empty_block->next = NULL;

	plane_nb = phy_block_nb % PLANES_PER_FLASH;
	mapping_index = plane_nb * FLASH_NB + phy_flash_nb;
	curr_root_entry = (empty_block_root*)empty_block_table_start + mapping_index;

	if (curr_root_entry->empty_block_nb == 0)
		curr_root_entry->tail = curr_root_entry->next = new_empty_block;
	else
		curr_root_entry->tail = curr_root_entry->tail->next = new_empty_block;
	curr_root_entry->empty_block_nb++;
	total_empty_block_nb++;
	return SUCCESS;
}

int INSERT_VICTIM_BLOCK(empty_block_entry *full_block)
{
	victim_block_root *curr_root_entry;
	victim_block_entry *new_victim_block;
	inverse_block_mapping_entry *inverse_block_entry;
	inverse_block_entry = GET_INVERSE_BLOCK_MAPPING_ENTRY(full_block->phy_flash_nb, full_block->phy_block_nb);

	/* Alloc New victim block entry */
	new_victim_block = (victim_block_entry*)malloc(sizeof(victim_block_entry));
	if (new_victim_block == NULL)
		RERR(FAIL, "malloc fail\n");

	/* Copy the full block address */
	new_victim_block->phy_flash_nb = full_block->phy_flash_nb;
	new_victim_block->phy_block_nb = full_block->phy_block_nb;
	new_victim_block->valid_page_nb = &(inverse_block_entry->valid_page_nb);
	new_victim_block->prev = new_victim_block->next = NULL;

	int plane_nb = full_block->phy_block_nb % PLANES_PER_FLASH;
	int mapping_index = plane_nb * FLASH_NB + full_block->phy_flash_nb;
	curr_root_entry = (victim_block_root*)victim_block_table_start + mapping_index;

	/* Update victim block list */
	if (curr_root_entry->victim_block_nb == 0)
		curr_root_entry->tail = curr_root_entry->next = new_victim_block;
	else{
		new_victim_block->prev = curr_root_entry->tail;
		curr_root_entry->tail = curr_root_entry->tail->next = new_victim_block;
	}
	curr_root_entry->victim_block_nb++;

	free(full_block);

	/* Update the total number of victim block */
	total_victim_block_nb++;
	return SUCCESS;
}

int EJECT_VICTIM_BLOCK(victim_block_entry *victim_block)
{
	int plane_nb = victim_block->phy_block_nb % PLANES_PER_FLASH;
	int mapping_index = plane_nb * FLASH_NB + victim_block->phy_flash_nb;
	victim_block_root *curr_root_entry;
	curr_root_entry = (victim_block_root*)victim_block_table_start + mapping_index;

	/* Update victim block list */
	if (victim_block == curr_root_entry->next){
		if (curr_root_entry->victim_block_nb == 1)
			curr_root_entry->tail = curr_root_entry->next = NULL;
		else{
			curr_root_entry->next = victim_block->next;
			curr_root_entry->next->prev = NULL;
		}
	}
	else if (victim_block == curr_root_entry->tail){
		curr_root_entry->tail = victim_block->prev;
		curr_root_entry->tail->next = NULL;
	}
	else{
		victim_block->prev->next = victim_block->next;
		victim_block->next->prev = victim_block->prev;
	}

	curr_root_entry->victim_block_nb--;
	total_victim_block_nb--;
	free(victim_block);
	return SUCCESS;
}

inverse_block_mapping_entry *GET_INVERSE_BLOCK_MAPPING_ENTRY(unsigned int phy_flash_nb, unsigned int phy_block_nb)
{
	int64_t mapping_index = phy_flash_nb * BLOCK_NB + phy_block_nb;

	inverse_block_mapping_entry* mapping_entry = (inverse_block_mapping_entry*)inverse_block_mapping_table_start + mapping_index;

	return mapping_entry;
}

uint32_t GET_INVERSE_MAPPING_INFO(uint32_t ppn)
{
	return (uint32_t)inverse_page_mapping_table[ppn];
}

int UPDATE_INVERSE_PAGE_MAPPING(uint32_t ppn, uint32_t lpn)
{
	inverse_page_mapping_table[ppn] = lpn;
	return SUCCESS;
}

int UPDATE_INVERSE_BLOCK_MAPPING(unsigned int phy_flash_nb, unsigned int phy_block_nb, int type)
{
	int i;
	inverse_block_mapping_entry *mapping_entry = GET_INVERSE_BLOCK_MAPPING_ENTRY(phy_flash_nb, phy_block_nb);
	mapping_entry->type = type;
	if (type == EMPTY_BLOCK)
		for (i = 0; i < PAGE_NB; i++)
			UPDATE_INVERSE_BLOCK_VALIDITY(phy_flash_nb, phy_block_nb, i, 0);
	return SUCCESS;
}

int UPDATE_INVERSE_BLOCK_VALIDITY(unsigned int phy_flash_nb, unsigned int phy_block_nb, unsigned int phy_page_nb, int valid)
{
	if (phy_flash_nb >= FLASH_NB || phy_block_nb >= BLOCK_NB || phy_page_nb >= PAGE_NB)
		RERR(FAIL, "Wrong physical address\n");

	int i;
	int valid_count = 0;
	inverse_block_mapping_entry *mapping_entry = GET_INVERSE_BLOCK_MAPPING_ENTRY(phy_flash_nb, phy_block_nb);
	char *valid_array = mapping_entry->valid_array;

	if (valid == VALID)
		valid_array[phy_page_nb] = 'V';
	else if (valid == INVALID)
		valid_array[phy_page_nb] = 'I';
	else if (valid == 0)
		valid_array[phy_page_nb] = '0';
	else
		PERR("Wrong valid value\n");

	/* Update valid_page_nb */
	for (i = 0; i < PAGE_NB; i++)
		if (valid_array[i] == 'V')
			valid_count++;
	mapping_entry->valid_page_nb = valid_count;
	return SUCCESS;
}
