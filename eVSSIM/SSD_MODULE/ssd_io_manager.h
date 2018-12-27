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

/* SSD Module Debugging */
void SSD_PRINT_STAMP(void);

/* SSD statistics */
double SSD_UTIL_CALC(void);
double SSD_WRITE_BANDWIDTH(void);
double SSD_READ_BANDWIDTH(void);

#endif
