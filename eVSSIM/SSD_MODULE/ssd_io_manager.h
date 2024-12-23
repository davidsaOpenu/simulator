// Copyright(c)2013
//
// Hanyang University, Seoul, Korea
// Embedded Software Systems Lab. All right reserved

#ifndef _SSD_IO_MANAGER_H
#define _SSD_IO_MANAGER_H

//#include "ssd_util.h"
#include "ftl.h"
#include "logging_statistics.h"

extern enum SSDTimeMode{
    REAL,
    EMULATED
}SSDTimeMode;

#define GET_TIME_MICROSEC(t) int64_t t;\
    switch(SSDTimeMode){\
        case REAL:\
           t = get_usec();\
           break;\
        case EMULATED:\
            t = time_delay;\
            break;\
        default:\
            t = 0;\
            break;\
    };\

/** @struct ssd_disk
 *  @brief This structure represents statistics related to ssd disk
 *  @var int occupied_pages_counter
 *  Member 'occupied_pages_counter' holds number of current occupied pages in disk
 *  @var int physical_page_writes
 *  Member 'physical_page_writes' holds sum of all page writes
 *  @var int logical_page_writes
 *  Member 'logical_page_writes' holds sum of logical only page writes
 *  *  @var int prev_channel_mode
 *  Member 'prev_channel_mode' holds the previous command that ran on this channel
 *  @var int prev_channel_mode
 *  Member 'cur_channel_mode' holds the current command that runs on this channel
 *  @var SSDStatistics* current_stats
 *  Member 'current_stats' holds stats of browser monitor
 */
typedef struct {

    uint64_t occupied_pages_counter;
    uint64_t physical_page_writes;
    uint64_t logical_page_writes;
    int* prev_channel_mode;
    int* cur_channel_mode;
    SSDStatistics* current_stats;

} ssd_disk;

extern unsigned int old_channel_nb;
extern int64_t io_alloc_overhead;
extern int64_t io_update_overhead;

extern int64_t time_delay;

/* Get Current time in micro second */
int64_t get_usec(void);

/* Insert delay on x usec. depending on config, will actually wait realworld time, otherwise do nothing. could have been a macro but his is more readable in code*/
static inline void wait_usec(int64_t usec){
    switch(SSDTimeMode){
        case REAL:
            {
                int64_t end = get_usec() + usec;
                while(end > get_usec());
            }
            break;
        case EMULATED:
            time_delay += usec;
            break;
    }
};

/* Initialize SSD Module */
int SSD_IO_INIT(void);
int SSD_IO_TERM(void);

/* GET IO from FTL */
ftl_ret_val SSD_PAGE_READ(unsigned int flash_nb, unsigned int block_nb, unsigned int page_nb, int offset, int type);
ftl_ret_val SSD_PAGE_WRITE(unsigned int flash_nb, unsigned int block_nb, unsigned int page_nb, int offset, int type);
ftl_ret_val SSD_BLOCK_ERASE(unsigned int flash_nb, unsigned int block_nb);
ftl_ret_val SSD_PAGE_COPYBACK(uint32_t source, uint32_t destination, int type);

/* Channel Access Delay */
int SSD_CH_ENABLE(unsigned int flash_nb, unsigned int channel);

/* Flash or Register Access */
int SSD_FLASH_ACCESS(unsigned int flash_nb, unsigned int channel, unsigned int reg);
int SSD_REG_ACCESS(unsigned int flash_nb, int channel, int reg);

/* Channel Delay */
int64_t SSD_CH_SWITCH_DELAY(unsigned int flash_nb, int channel);

/* Register Delay */
int SSD_REG_WRITE_DELAY(unsigned int flash_nb, int channel, int reg);
int SSD_REG_READ_DELAY(unsigned int flash_nb, int channel, int reg);

/* Cell Delay */
int SSD_CELL_WRITE_DELAY(int reg);
int SSD_CELL_READ_DELAY(int reg);

/* Erase Delay */
int SSD_BLOCK_ERASE_DELAY(int reg);

/* Mark Time Stamp */
ftl_ret_val SSD_REG_RECORD(int reg, int type, int offset, int channel);

/* Check Read Operation in the Same Channel  */
int SSD_CH_ACCESS(unsigned int flash_nb, int channel);
int64_t SSD_GET_CH_ACCESS_TIME_FOR_READ(int channel, int reg);
void SSD_UPDATE_CH_ACCESS_TIME(int channel, int64_t current_time);

/* Correction Delay */
void SSD_UPDATE_IO_REQUEST(int reg);
void SSD_UPDATE_IO_OVERHEAD(int reg, int64_t overhead_time);
void SSD_REMAIN_IO_DELAY(unsigned int flash_nb, int channel, int reg);
void SSD_UPDATE_QEMU_OVERHEAD(int64_t delay);

/* SSD Module Debugging */
void SSD_PRINT_STAMP(void);

double SSD_UTIL(void);

#endif
