// Copyright(c)2013 
//
// Hanyang University, Seoul, Korea
// Embedded Software Systems Lab. All right reserved

#ifndef _INVERSE_MAPPING_MANAGER_H_
#define _INVERSE_MAPPING_MANAGER_H_

extern uint32_t* inverse_page_mapping_table;

extern int64_t total_empty_block_nb;
extern int64_t total_victim_block_nb;

extern void* empty_block_table_start;
extern void* victim_block_table_start;

extern unsigned int empty_block_table_index;

typedef struct inverse_block_mapping_entry
{
	int valid_page_nb;
	int type;
	unsigned int erase_count;
	char* valid_array;

}inverse_block_mapping_entry;

typedef struct empty_block_root
{
	struct empty_block_entry* next;
	struct empty_block_entry* tail;
	unsigned int empty_block_nb;
}empty_block_root;

typedef struct empty_block_entry
{
	unsigned int phy_flash_nb;
	unsigned int phy_block_nb;
	unsigned int curr_phy_page_nb;
	struct empty_block_entry* next;

}empty_block_entry;

typedef struct victim_block_root
{
	struct victim_block_entry* next;
	struct victim_block_entry* tail;
	unsigned int victim_block_nb;
}victim_block_root;

typedef struct victim_block_entry
{
	unsigned int phy_flash_nb;
	unsigned int phy_block_nb;
	int *valid_page_nb;
	struct victim_block_entry* prev;
	struct victim_block_entry* next;
}victim_block_entry;

extern victim_block_entry* victim_block_list_head;
extern victim_block_entry* victim_block_list_tail;

void INIT_INVERSE_PAGE_MAPPING(void);
void INIT_INVERSE_BLOCK_MAPPING(void);
void INIT_EMPTY_BLOCK_LIST(void);
void INIT_VICTIM_BLOCK_LIST(void);
void INIT_VALID_ARRAY(void);

void TERM_INVERSE_PAGE_MAPPING(void);
void TERM_INVERSE_BLOCK_MAPPING(void);
void TERM_EMPTY_BLOCK_LIST(void);
void TERM_VICTIM_BLOCK_LIST(void);
void TERM_VALID_ARRAY(void);

empty_block_entry* GET_EMPTY_BLOCK(int mode, int mapping_index);
ftl_ret_val INSERT_EMPTY_BLOCK(unsigned int phy_flash_nb, unsigned int phy_block_nb);

ftl_ret_val INSERT_VICTIM_BLOCK(empty_block_entry* full_block);
int EJECT_VICTIM_BLOCK(victim_block_entry* victim_block);

inverse_block_mapping_entry* GET_INVERSE_BLOCK_MAPPING_ENTRY(unsigned int phy_flash_nb, unsigned int phy_block_nb);

uint32_t GET_INVERSE_MAPPING_INFO(uint32_t lpn);
int UPDATE_INVERSE_PAGE_MAPPING(uint32_t ppn, uint32_t lpn);
int UPDATE_INVERSE_BLOCK_MAPPING(unsigned int phy_flash_nb,
                                 unsigned int phy_block_nb,
								 int type);
ftl_ret_val UPDATE_INVERSE_BLOCK_VALIDITY(unsigned int phy_flash_nb,
                                          unsigned int phy_block_nb,
										  unsigned int phy_page_nb,
										  char valid);


#endif
