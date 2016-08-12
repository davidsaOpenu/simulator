// Copyright(c)2013 
//
// Hanyang University, Seoul, Korea
// Embedded Software Systems Lab. All right reserved

#ifndef _SSD_IO_MANAGER_H
#define _SSD_IO_MANAGER_H

#include "ssd_util.h"

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

/* Channel Access Delay */
int SSD_CH_ENABLE(int channel);

/* Flash or Register Access */
int SSD_FLASH_ACCESS(unsigned int flash_nb, int reg);
int SSD_REG_ACCESS(int reg);

/* Channel Delay */
int64_t SSD_CH_SWITCH_DELAY(int channel);

/* Register Delay */
int SSD_REG_WRITE_DELAY(int reg);
int SSD_REG_READ_DELAY(int reg);

/* Cell Delay */
int SSD_CELL_WRITE_DELAY(int reg);
int SSD_CELL_READ_DELAY(int reg);

/* Erase Delay */
int SSD_BLOCK_ERASE_DELAY(int reg);

/* Mark Time Stamp */
int SSD_CH_RECORD(int channel, int cmd, int offset, int ret);
int SSD_REG_RECORD(int reg, int cmd, int type, int offset, int channel);
int SSD_CELL_RECORD(int reg, int cmd);

/* Check Read Operation in the Same Channel  */
int SSD_CH_ACCESS(int channel);
int64_t SSD_GET_CH_ACCESS_TIME_FOR_READ(int channel, int reg);
void SSD_UPDATE_CH_ACCESS_TIME(int channel, int64_t current_time);

/* Correction Delay */
void SSD_UPDATE_IO_REQUEST(int reg);
void SSD_UPDATE_IO_OVERHEAD(int reg, int64_t overhead_time);
void SSD_REMAIN_IO_DELAY(int reg);
void SSD_UPDATE_QEMU_OVERHEAD( int64_t delay);

/* SSD Module Debugging */
void SSD_PRINT_STAMP(void);

#endif
