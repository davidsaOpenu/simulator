// Copyright(c)2013 
//
// Hanyang University, Seoul, Korea
// Embedded Software Systems Lab. All right reserved

#ifndef _SSD_IO_MANAGER_H
#define _SSD_IO_MANAGER_H

//#include "ssd_util.h"
#include "ftl.h"


extern int old_channel_nb;
extern int64_t io_alloc_overhead;
extern int64_t io_update_overhead;

/* Get Current time in micro second */
int64_t get_usec(void);

/* Initialize SSD Module */
int SSD_IO_INIT(void);
int SSD_IO_TERM(void);

/* GET IO from FTL */
ftl_ret_val SSD_PAGE_READ(unsigned int flash_nb, unsigned int block_nb, unsigned int page_nb, int offset, int type, int io_page_nb);
ftl_ret_val SSD_PAGE_WRITE(unsigned int flash_nb, unsigned int block_nb, unsigned int page_nb, int offset, int type, int io_page_nb);
ftl_ret_val SSD_BLOCK_ERASE(unsigned int flash_nb, unsigned int block_nb);
ftl_ret_val SSD_PAGE_COPYBACK(uint32_t source, uint32_t destination, int type);

/*
 * registers mode controllers
 * */
// Turn register into read mode
void _SSD_SET_REGISTER_READ(unsigned int flash_nb);
// Turn register into write mode
void _SSD_REGISTER_WRITE(unsigned int flash_nb);

/*
 * blocks mode controllers
 * */
// Turn block into cell programming mode
void _SSD_SET_BLOCK_CELL_PROGRAMMING(unsigned int flash_nb, unsigned int block_nb);
// Turn block into read mode
void _SSD_SET_BLOCK_READ(unsigned int flash_nb, unsigned int block_nb);
// Turn block into erase mode
void _SSD_SET_BLOCK_ERASE(unsigned int flash_nb, unsigned int block_nb);
// Turn block to page copyback mode
void _SSD_SET_BLOCK_PAGE_COPYBACK(unsigned int flash_nb, unsigned int block_nb);

/*
 * IO wait controllers
 * */
// wait until block @block_nb on flash @flash_nb
// get available for new operations
void _SSD_IO_WAIT(unsigned int flash_nb, unsigned int block_nb);
// wait until channel @channel
// get available for access
void _SSD_CHANNEL_ACCESS(int channel);
// busy wait until timestamp equals or after @until_timestamp
void _SSD_BUSY_WAIT(int64_t until_timestamp);

/* SSD Module Debugging */
void SSD_PRINT_STAMP(void);

/* SSD statistics */
unsigned long int SSD_GET_LOGICAL_PAGE_WRITE_COUNT(void);
unsigned long int SSD_GET_PHYSICAL_PAGE_WRITE_COUNT(void);
unsigned long int SSD_GET_LOGICAL_PAGE_READ_COUNT(void);
unsigned long int SSD_GET_PHYSICAL_PAGE_READ_COUNT(void);
unsigned long int SSD_GET_ERASE_COUNT(void);
double SSD_UTIL_CALC(void);
double SSD_WRITE_BANDWIDTH(void);
double SSD_READ_BANDWIDTH(void);

#endif
