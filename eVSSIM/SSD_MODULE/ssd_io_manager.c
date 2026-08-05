// Copyright(c)2013
//
// Hanyang University, Seoul, Korea
// Embedded Software Systems Lab. All right reserved

#include "common.h"
#include <string.h>
#include <stdlib.h>
#include "test_context.h"
#include "logging_parser.h"

ssd_manager_t* ssds_manager = NULL;

char ssd_version[4] = "1.0";
char ssd_date[9] = "13.04.11";

struct timeval logging_parser_tv;

int64_t time_delay = 0;

enum SSDTimeMode SSDTimeMode;

/* Emulation time state */
static int64_t start_wall_time_us = 0; /* wall-clock anchor captured at init */
static int64_t sim_time_us = 0;        /* simulated time offset */
static uint64_t log_seq_id = 0;        /* optional: used where you assign seq ids TODO: Apply this to logs*/

/**
 * Get current time in microseconds
 * @return Current time in microseconds
 */
int64_t get_usec(void)
{
    if (SSDTimeMode == EMULATED)
    {
        // Base Timestamp + Offset + Constant Time Delay
        return start_wall_time_us + __atomic_load_n(&sim_time_us, __ATOMIC_ACQUIRE) + __atomic_load_n(&time_delay, __ATOMIC_RELAXED);
    }
    return 0;
}

/**
 * Advance simulation time by specified microseconds
 */
void wait_usec(int64_t usec)
{
    if (usec <= 0)
        return;

    if (SSDTimeMode == EMULATED)
    {
        __atomic_fetch_add(&sim_time_us, usec, __ATOMIC_ACQ_REL);
    }
}

/** Wait until a specific target time is reached
 *  @param target_us Target time in microseconds to wait until
 */
void wait_until(int64_t target_us)
{
    int64_t now = get_usec();
    if (target_us > now)
        wait_usec(target_us - now);
}

int SSD_IO_INIT(uint8_t device_index){

    uint32_t i= 0;

    /* Print SSD version */
    PINFO("SSD Emulator Version: %s ver. (%s)\n", ssd_version, ssd_date);

    /* Init logging and timestamp related vars */
    struct timeval tv;
    gettimeofday(&tv, NULL);
    start_wall_time_us = (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
    sim_time_us = 0;
    log_seq_id = 0;

    /* Init Variable for Channel Switch Delay */
    __atomic_store_n(&ssds_manager[device_index].old_channel_nb, devices[device_index].channel_nb, __ATOMIC_RELEASE);
    __atomic_store_n(&ssds_manager[device_index].last_operation_time_us, start_wall_time_us, __ATOMIC_RELEASE);

    /* Init ssd statistic */
    __atomic_store_n(&ssds_manager[device_index].ssd.occupied_pages_counter, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&ssds_manager[device_index].ssd.physical_page_writes, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&ssds_manager[device_index].ssd.logical_page_writes, 0, __ATOMIC_RELAXED);

    /* Init Variable for Time-stamp */

    /* Init Command and Command type */
    ssds_manager[device_index].reg_io_cmd = (int *)malloc(sizeof(int) * devices[device_index].flash_nb * devices[device_index].planes_per_flash);
    for(i=0; i< devices[device_index].flash_nb*devices[device_index].planes_per_flash; i++){
        __atomic_store_n(&ssds_manager[device_index].reg_io_cmd[i], NOOP, __ATOMIC_RELAXED);
    }

    ssds_manager[device_index].reg_io_type = (int *)malloc(sizeof(int) * devices[device_index].flash_nb * devices[device_index].planes_per_flash);
    for(i=0; i< devices[device_index].flash_nb*devices[device_index].planes_per_flash; i++){
        __atomic_store_n(&ssds_manager[device_index].reg_io_type[i], NOOP, __ATOMIC_RELAXED);
    }

    /* Init Register and Flash IO Time */
    ssds_manager[device_index].reg_io_time = (int64_t *)malloc(sizeof(int64_t) * devices[device_index].flash_nb * devices[device_index].planes_per_flash);
    for(i=0; i<devices[device_index].flash_nb*devices[device_index].planes_per_flash; i++){
        __atomic_store_n(&ssds_manager[device_index].reg_io_time[i], -1, __ATOMIC_RELAXED);
    }

    ssds_manager[device_index].cell_io_time = (int64_t *)malloc(sizeof(int64_t) * devices[device_index].flash_nb * devices[device_index].planes_per_flash);
    for(i=0; i< devices[device_index].flash_nb*devices[device_index].planes_per_flash; i++){
        __atomic_store_n(&ssds_manager[device_index].cell_io_time[i], -1, __ATOMIC_RELAXED);
    }

    ssds_manager[device_index].ssd.prev_channel_mode = (int *)malloc(sizeof(int)*devices[device_index].channel_nb);
    ssds_manager[device_index].ssd.cur_channel_mode = (int *)malloc(sizeof(int)*devices[device_index].channel_nb);
    for (i = 0; i < devices[device_index].channel_nb; i++) {
        __atomic_store_n(&ssds_manager[device_index].ssd.prev_channel_mode[i], NOOP, __ATOMIC_RELAXED);
        __atomic_store_n(&ssds_manager[device_index].ssd.cur_channel_mode[i], NOOP, __ATOMIC_RELAXED);
    }

    SSDTimeMode = EMULATED;

    return 0;
}

int SSD_IO_TERM(uint8_t device_index)
{
    free(ssds_manager[device_index].reg_io_cmd);
    free(ssds_manager[device_index].reg_io_type);
    free(ssds_manager[device_index].reg_io_time);
    free(ssds_manager[device_index].cell_io_time);

    free(ssds_manager[device_index].ssd.prev_channel_mode);
    free(ssds_manager[device_index].ssd.cur_channel_mode);

    SSD_CLEAR_TEST_CONTEXT();

    return 0;
}

static ftl_ret_val SSD_CELL_RECORD(uint8_t device_index, int reg, int channel)
{
    ftl_ret_val retval = FTL_SUCCESS;
    int channel_mode = __atomic_load_n(&ssds_manager[device_index].ssd.cur_channel_mode[channel], __ATOMIC_ACQUIRE);
    switch (channel_mode) {
        case WRITE:
            __atomic_store_n(&ssds_manager[device_index].cell_io_time[reg],
                             __atomic_load_n(&ssds_manager[device_index].last_operation_time_us, __ATOMIC_ACQUIRE) + devices[device_index].reg_write_delay,
                             __ATOMIC_RELEASE);
            break;
        case READ:
            __atomic_store_n(&ssds_manager[device_index].cell_io_time[reg],
                             __atomic_load_n(&ssds_manager[device_index].last_operation_time_us, __ATOMIC_ACQUIRE),
                             __ATOMIC_RELEASE);
            break;
        case ERASE: // fallthrough
        case COPYBACK:
            __atomic_store_n(&ssds_manager[device_index].cell_io_time[reg], get_usec(), __ATOMIC_RELEASE);
            break;
        default:
            PERR("Unexpected current channel mode = %d\n", channel_mode)
            retval = FTL_FAILURE;
    }

    return retval;
}

static int SSD_CH_RECORD(uint8_t device_index, int channel, int offset, int ret)
{
    int prev_channel_mode = __atomic_load_n(&ssds_manager[device_index].ssd.prev_channel_mode[channel], __ATOMIC_ACQUIRE);
    if (prev_channel_mode == READ && offset != 0 && ret == 0){
        __atomic_fetch_add(&ssds_manager[device_index].last_operation_time_us, devices[device_index].channel_switch_delay_w, __ATOMIC_ACQ_REL);
    }
    else if (prev_channel_mode == WRITE && offset != 0 && ret == 0) {
        __atomic_fetch_add(&ssds_manager[device_index].last_operation_time_us, devices[device_index].channel_switch_delay_r, __ATOMIC_ACQ_REL);
    }
    else {
        __atomic_store_n(&ssds_manager[device_index].last_operation_time_us, get_usec(), __ATOMIC_RELEASE);
    }
    return FTL_SUCCESS;
}

ftl_ret_val SSD_PAGE_WRITE(uint8_t device_index, unsigned int flash_nb, unsigned int block_nb, unsigned int page_nb, int offset, int type)
{
    uint32_t channel, reg;
    int ret = FTL_FAILURE;
    int delay_ret = 0;

    int64_t start = get_usec();

    /* Calculate ch & reg */
    channel = flash_nb % devices[device_index].channel_nb;
    __atomic_store_n(&ssds_manager[device_index].ssd.cur_channel_mode[channel], WRITE, __ATOMIC_RELEASE);
    reg = flash_nb*devices[device_index].planes_per_flash + block_nb%devices[device_index].planes_per_flash;

    /* Delay Operation */
    SSD_CH_ENABLE(device_index, flash_nb, channel); // Channel enable

    if (devices[device_index].io_parallelism == 0 ){
        delay_ret = SSD_FLASH_ACCESS(device_index, flash_nb, channel);
    }
    else{
        delay_ret = SSD_REG_ACCESS(device_index, flash_nb, channel, reg);
    }

    /* Check Channel Operation */
    while(ret == FTL_FAILURE){
        ret = SSD_CH_ACCESS(device_index, flash_nb, channel);
    }

    /* Record Time Stamp */
    SSD_CH_RECORD(device_index, channel, offset, delay_ret);
    SSD_CELL_RECORD(device_index, reg, channel);
    SSD_REG_RECORD(device_index, reg, type, channel);
    SSD_REG_ACCESS(device_index, flash_nb, channel, reg);

    int64_t end = get_usec();;
    if (__atomic_load_n(&ssds_manager[device_index].old_channel_nb, __ATOMIC_ACQUIRE) == channel && __atomic_load_n(&ssds_manager[device_index].ssd.prev_channel_mode[channel], __ATOMIC_ACQUIRE) != WRITE) { //if channel is same but only mode is different
        // PINFO("change to write for channel %d\n", channel);
        LOG_CHANNEL_SWITCH_TO_WRITE(GET_LOGGER(device_index, flash_nb), (ChannelSwitchToWriteLog) {
            .channel = channel,
            .metadata = LOG_META(device_index, start, end)
        });
    }
    __atomic_store_n(&ssds_manager[device_index].ssd.prev_channel_mode[channel], WRITE, __ATOMIC_RELEASE);
    __atomic_store_n(&ssds_manager[device_index].old_channel_nb, channel, __ATOMIC_RELEASE);

    /* Update ssd page write counters */
    if (type != WRITE_COMMIT) {
        __atomic_fetch_add(&ssds_manager[device_index].ssd.occupied_pages_counter, 1, __ATOMIC_RELAXED);
        SSD_UTIL_LOG(device_index, flash_nb);
    }
    __atomic_fetch_add(&ssds_manager[device_index].ssd.physical_page_writes, 1, __ATOMIC_RELAXED);

    inverse_block_mapping_entry* block_entry = GET_INVERSE_BLOCK_MAPPING_ENTRY(device_index, flash_nb, block_nb);
    block_entry->dirty_page_nb++;

    if (type == WRITE_COMMIT) {
        LOG_PHYSICAL_CELL_PROGRAM_COMPATIBLE(GET_LOGGER(device_index, flash_nb), (PhysicalCellProgramCompatibleLog) {
            .channel = channel, .block = block_nb, .page = page_nb,
            .metadata = LOG_META(device_index, start, end)
        });
    }
    else {
        LOG_PHYSICAL_CELL_PROGRAM(GET_LOGGER(device_index, flash_nb), (PhysicalCellProgramLog) {
            .channel = channel, .block = block_nb, .page = page_nb,
            .metadata = LOG_META(device_index, start, end),
            .background = (type == GC_WRITE_BACKGROUND),
        });
    }


    if (type == WRITE || type == WRITE_COMMIT) { // if we log logical write first, write amp may get negative
        __atomic_fetch_add(&ssds_manager[device_index].ssd.logical_page_writes, 1, __ATOMIC_RELAXED);

        LOG_LOGICAL_CELL_PROGRAM(GET_LOGGER(device_index, flash_nb),(LogicalCellProgramLog) {
            .channel = channel, .block = block_nb, .page = page_nb,
            .metadata = LOG_META(device_index, start, end)
        });
    }

    return ret;
}

ftl_ret_val SSD_PAGE_READ(uint8_t device_index, unsigned int flash_nb, unsigned int block_nb, unsigned int page_nb, int offset, int type)
{
    unsigned int channel, reg;
    int delay_ret = 0;

    int64_t start = get_usec();

    /* Calculate ch & reg */
    channel = flash_nb % devices[device_index].channel_nb;
    __atomic_store_n(&ssds_manager[device_index].ssd.cur_channel_mode[channel], READ, __ATOMIC_RELEASE);
    reg = flash_nb*devices[device_index].planes_per_flash + block_nb%devices[device_index].planes_per_flash;

    /* Delay Operation */
    SSD_CH_ENABLE(device_index, flash_nb, channel);    // channel enable

    /* Access Register */
    if( devices[device_index].io_parallelism == 0 ){
        delay_ret = SSD_FLASH_ACCESS(device_index, flash_nb, channel);
    }
    else{
        delay_ret = SSD_REG_ACCESS(device_index, flash_nb, channel, reg);
    }

    /* Record Time Stamp */
    SSD_CH_RECORD(device_index, channel, offset, delay_ret);
    SSD_CELL_RECORD(device_index, reg, channel);
    SSD_REG_RECORD(device_index, reg, type, channel);
    SSD_REG_ACCESS(device_index, flash_nb, channel, reg);

    int64_t end = get_usec();

    if (__atomic_load_n(&ssds_manager[device_index].old_channel_nb, __ATOMIC_ACQUIRE) == channel && __atomic_load_n(&ssds_manager[device_index].ssd.prev_channel_mode[channel], __ATOMIC_ACQUIRE) != READ && __atomic_load_n(&ssds_manager[device_index].ssd.prev_channel_mode[channel], __ATOMIC_ACQUIRE) != NOOP) {
        LOG_CHANNEL_SWITCH_TO_READ(GET_LOGGER(device_index, flash_nb), (ChannelSwitchToReadLog) {
            .channel = channel,
            .metadata = LOG_META(device_index, start, end)
        });
    }
    __atomic_store_n(&ssds_manager[device_index].ssd.prev_channel_mode[channel], READ, __ATOMIC_RELEASE);
    __atomic_store_n(&ssds_manager[device_index].old_channel_nb, channel, __ATOMIC_RELEASE);

    LOG_PHYSICAL_CELL_READ(GET_LOGGER(device_index, flash_nb), (PhysicalCellReadLog) {
        .channel = channel, .block = block_nb, .page = page_nb,
        .metadata = LOG_META(device_index, start, end),
        .background = (type == GC_READ_BACKGROUND),
    });

    return FTL_SUCCESS;
}

ftl_ret_val SSD_BLOCK_ERASE(uint8_t device_index, unsigned int flash_nb, unsigned int block_nb, int type)
{
    int channel, reg;

    int64_t start = get_usec();

    /* Calculate ch & reg */
    channel = flash_nb % devices[device_index].channel_nb;
    __atomic_store_n(&ssds_manager[device_index].ssd.cur_channel_mode[channel], ERASE, __ATOMIC_RELEASE);
    reg = flash_nb*devices[device_index].planes_per_flash + block_nb%devices[device_index].planes_per_flash;

    /* Delay Operation */
    if( devices[device_index].io_parallelism == 0 ){
        SSD_FLASH_ACCESS(device_index, flash_nb, channel);
    }
    else{
        SSD_REG_ACCESS(device_index, flash_nb, channel, reg);
    }

    /* Record Time Stamp */
    SSD_REG_RECORD(device_index, reg, ERASE, channel);
    SSD_CELL_RECORD(device_index, reg, channel);

    int64_t end = get_usec();

    inverse_block_mapping_entry* block_entry = GET_INVERSE_BLOCK_MAPPING_ENTRY(device_index, flash_nb, block_nb);

    __atomic_fetch_sub(&ssds_manager[device_index].ssd.occupied_pages_counter, block_entry->dirty_page_nb, __ATOMIC_RELAXED);
    SSD_UTIL_LOG(device_index, flash_nb);
    __atomic_store_n(&ssds_manager[device_index].ssd.prev_channel_mode[channel], ERASE, __ATOMIC_RELEASE);

    LOG_BLOCK_ERASE(GET_LOGGER(device_index, flash_nb), (BlockEraseLog) {
        .channel = channel, .die = flash_nb, .block = block_nb, .dirty_page_nb = block_entry->dirty_page_nb,
        .metadata = LOG_META(device_index, start, end),
        .background = (type == ERASE_BACKGROUND),
    });

    block_entry->dirty_page_nb = 0;

    return FTL_SUCCESS;
}

int SSD_FLASH_ACCESS(uint8_t device_index, unsigned int flash_nb, unsigned int channel)
{
    uint32_t i;
    uint32_t r_num = flash_nb * devices[device_index].planes_per_flash;
    int ret = 0;

    for (i=0;i<devices[device_index].planes_per_flash;i++) {

        ret = SSD_REG_ACCESS(device_index, flash_nb, channel, r_num);

        r_num++;
    }

    return ret;
}

int SSD_REG_ACCESS(uint8_t device_index, unsigned int flash_nb, int channel, int reg)
{
    int reg_io_cmd = __atomic_load_n(&ssds_manager[device_index].reg_io_cmd[reg], __ATOMIC_ACQUIRE);
    switch (reg_io_cmd){
        case READ:
            return SSD_REG_READ_DELAY(device_index, flash_nb, channel, reg) + SSD_CELL_READ_DELAY(device_index, reg);
        case WRITE:
            return SSD_REG_WRITE_DELAY(device_index, flash_nb, channel, reg) + SSD_CELL_WRITE_DELAY(device_index, reg);
        case ERASE:
            return SSD_BLOCK_ERASE_DELAY(device_index, reg);
        case COPYBACK:
            return SSD_CELL_READ_DELAY(device_index, reg) + SSD_CELL_WRITE_DELAY(device_index, reg);
        case NOOP:
            return 0;
        default:
            PERR("SSD_REG_ACCESS: Command Error! %d\n", reg_io_cmd);
            return 0;
    }
}

int SSD_CH_ENABLE(uint8_t device_index, unsigned int flash_nb, unsigned int channel)
{
    if(devices[device_index].channel_switch_delay_r == 0 && devices[device_index].channel_switch_delay_w == 0)
        return FTL_SUCCESS;

        //todo: currently writing on all channels at the same time takes more time than writing on one
    if(__atomic_load_n(&ssds_manager[device_index].old_channel_nb, __ATOMIC_ACQUIRE) != channel){
        SSD_CH_SWITCH_DELAY(device_index, flash_nb, channel);
    }

    return FTL_SUCCESS;
}

ftl_ret_val SSD_REG_RECORD(uint8_t device_index, int reg, int type, int channel)
{
    int channel_mode = __atomic_load_n(&ssds_manager[device_index].ssd.cur_channel_mode[channel], __ATOMIC_ACQUIRE);
    __atomic_store_n(&ssds_manager[device_index].reg_io_cmd[reg], channel_mode, __ATOMIC_RELEASE);
    __atomic_store_n(&ssds_manager[device_index].reg_io_type[reg], type, __ATOMIC_RELEASE);
    ftl_ret_val retval = FTL_SUCCESS;

    switch (channel_mode) {
        case WRITE:
            __atomic_store_n(&ssds_manager[device_index].reg_io_time[reg],
                             __atomic_load_n(&ssds_manager[device_index].last_operation_time_us, __ATOMIC_ACQUIRE),
                             __ATOMIC_RELEASE);
            if (__atomic_load_n(&ssds_manager[device_index].ssd.prev_channel_mode[channel], __ATOMIC_ACQUIRE) == READ) {
                __atomic_fetch_add(&ssds_manager[device_index].reg_io_time[reg], devices[device_index].channel_switch_delay_w, __ATOMIC_ACQ_REL);
            }
            // SSD_UPDATE_CH_ACCESS_TIME(channel, ssds_manager[device_index].reg_io_time[reg]);

        break;
    case READ:
        __atomic_store_n(&ssds_manager[device_index].reg_io_time[reg], SSD_GET_CH_ACCESS_TIME_FOR_READ(device_index, channel, reg), __ATOMIC_RELEASE);
        if (__atomic_load_n(&ssds_manager[device_index].ssd.prev_channel_mode[channel], __ATOMIC_ACQUIRE) != READ && __atomic_load_n(&ssds_manager[device_index].ssd.prev_channel_mode[channel], __ATOMIC_ACQUIRE) != NOOP) {
                __atomic_fetch_add(&ssds_manager[device_index].reg_io_time[reg], devices[device_index].channel_switch_delay_r, __ATOMIC_ACQ_REL);
            }
        break;
    case ERASE:
        break;
    case COPYBACK:
        __atomic_store_n(&ssds_manager[device_index].reg_io_time[reg],
                         __atomic_load_n(&ssds_manager[device_index].cell_io_time[reg], __ATOMIC_ACQUIRE) + devices[device_index].cell_read_delay,
                         __ATOMIC_RELEASE);
        break;
    default:
        PERR("SSD_REG_RECORD: Command Error! %d\n", channel_mode);
        retval = FTL_FAILURE;
        break;
    }
    return retval;
}

int SSD_CH_ACCESS(uint8_t device_index, unsigned int flash_nb, int channel)
{
    uint32_t i, j;
    int ret = FTL_SUCCESS;
    uint32_t r_num;

    for (i=0;i<devices[device_index].way_nb;i++) {
        r_num = channel*devices[device_index].planes_per_flash + i*devices[device_index].channel_nb*devices[device_index].planes_per_flash;
        for(j=0;j<devices[device_index].planes_per_flash;j++){
            int64_t reg_io_time = __atomic_load_n(&ssds_manager[device_index].reg_io_time[r_num], __ATOMIC_ACQUIRE);
            if(reg_io_time <= get_usec() && reg_io_time != -1){
                if(__atomic_load_n(&ssds_manager[device_index].reg_io_cmd[r_num], __ATOMIC_ACQUIRE) == READ){
                    SSD_CELL_READ_DELAY(device_index, r_num);
                    SSD_REG_READ_DELAY(device_index, flash_nb, channel, r_num);
                    ret = FTL_FAILURE;
                }
                else if(__atomic_load_n(&ssds_manager[device_index].reg_io_cmd[r_num], __ATOMIC_ACQUIRE) == WRITE){
                    SSD_REG_WRITE_DELAY(device_index, flash_nb, channel, r_num);
                    ret = FTL_FAILURE;
                }
            }
            r_num++;
        }
    }

    return ret;
}

int64_t SSD_CH_SWITCH_DELAY(uint8_t device_index, unsigned int flash_nb, int channel)
{
    int switch_delay = 0;

    int channel_mode = __atomic_load_n(&ssds_manager[device_index].ssd.cur_channel_mode[channel], __ATOMIC_ACQUIRE);
    if (channel_mode == READ ) {
        switch_delay = devices[device_index].channel_switch_delay_r;
    }
    else if (channel_mode == WRITE) {
        switch_delay = devices[device_index].channel_switch_delay_w;
    }
    else {
        return 0;
    }

    int64_t start = get_usec();
    int64_t diff = start - __atomic_load_n(&ssds_manager[device_index].last_operation_time_us, __ATOMIC_ACQUIRE);

#ifdef DEL_QEMU_OVERHEAD
    if(diff < switch_delay){
        SSD_UPDATE_QEMU_OVERHEAD(device_index, switch_delay-diff);
    }
#endif

    if (diff < switch_delay) {
        wait_usec(switch_delay - diff);
    }

    int64_t end = get_usec();

    switch(channel_mode){
        case READ:{
            LOG_CHANNEL_SWITCH_TO_READ(GET_LOGGER(device_index, flash_nb), (ChannelSwitchToReadLog) {
                .channel = channel,
                .metadata = LOG_META(device_index, start, end)
            });
            break;
        }

        case WRITE:{
            // PINFO("write first write to channel %d\n", channel);
            LOG_CHANNEL_SWITCH_TO_WRITE(GET_LOGGER(device_index, flash_nb), (ChannelSwitchToWriteLog) {
                .channel = channel,
                .metadata = LOG_META(device_index, start, end)
            });
            break;
        }

        case COPYBACK:{
            //TODO: log channel switch to copyback
            break;
        }

        default:{
            RERR(end - start, "New Channel mode unexpected : %d", channel_mode);
            break;
        }
    }
    return end - start;
}

int SSD_REG_WRITE_DELAY(uint8_t device_index, unsigned int flash_nb, int channel, int reg)
{
    int ret = 0;
    int64_t diff = 0;
    const int64_t time_stamp = __atomic_load_n(&ssds_manager[device_index].reg_io_time[reg], __ATOMIC_ACQUIRE);

    /* Absolute scheduled start for this register op */
    if (time_stamp == -1)
        return 0;

    int64_t start = get_usec();
    diff = start - time_stamp;

#ifdef DEL_QEMU_OVERHEAD
    if(diff < devices[device_index].reg_write_delay){
        SSD_UPDATE_QEMU_OVERHEAD(device_index, devices[device_index].reg_write_delay-diff);
    }
    diff = start - __atomic_load_n(&ssds_manager[device_index].reg_io_time[reg], __ATOMIC_ACQUIRE);
#endif

    if (diff < devices[device_index].reg_write_delay){
        wait_usec(devices[device_index].reg_write_delay - diff);
        ret = 1;
    }

    /* Update Time Stamp Struct */
    __atomic_store_n(&ssds_manager[device_index].reg_io_time[reg], -1, __ATOMIC_RELEASE);
    __atomic_store_n(&ssds_manager[device_index].reg_io_cmd[reg], NOOP, __ATOMIC_RELEASE);

    int64_t end = get_usec();

    LOG_REGISTER_WRITE(GET_LOGGER(device_index, flash_nb), (RegisterWriteLog) {
        .channel = channel, .die = flash_nb, .reg = reg,
        .metadata = LOG_META(device_index, start, end)
    });

    return ret;
}

int SSD_REG_READ_DELAY(uint8_t device_index, unsigned int flash_nb, int channel, int reg)
{
    int ret = 0;
    int64_t start = 0;
    int64_t end = 0;
    int64_t diff = 0;
    int64_t time_stamp = __atomic_load_n(&ssds_manager[device_index].reg_io_time[reg], __ATOMIC_ACQUIRE);

    start = get_usec();

    if (time_stamp == -1)
        return 0;

    /* Reg Read Delay */
    start = get_usec();
    diff = start - time_stamp;

#ifdef DEL_QEMU_OVERHEAD
    if(diff < devices[device_index].reg_read_delay){
        SSD_UPDATE_QEMU_OVERHEAD(device_index, devices[device_index].reg_read_delay - diff);
    }
    diff = start - __atomic_load_n(&ssds_manager[device_index].reg_io_time[reg], __ATOMIC_ACQUIRE);
#endif

    if(diff < devices[device_index].reg_read_delay){
        wait_usec(devices[device_index].reg_read_delay - diff);
        ret = 1;
    }

    end = get_usec();

    /* Update Time Stamp Struct */
    __atomic_store_n(&ssds_manager[device_index].reg_io_time[reg], -1, __ATOMIC_RELEASE);
    __atomic_store_n(&ssds_manager[device_index].reg_io_cmd[reg], NOOP, __ATOMIC_RELEASE);

    LOG_REGISTER_READ(GET_LOGGER(device_index, flash_nb), (RegisterReadLog) {
        .channel = channel, .die = flash_nb, .reg = reg,
        .metadata = LOG_META(device_index, start, end)
    });

    return ret;
}

int SSD_CELL_WRITE_DELAY(uint8_t device_index, int reg)
{
    int ret = 0;
    int64_t start = 0;
    int64_t diff = 0;
    int64_t time_stamp = __atomic_load_n(&ssds_manager[device_index].cell_io_time[reg], __ATOMIC_ACQUIRE);

    if (time_stamp == -1)
        return 0;
    /* Cell Write Delay */
    start = get_usec();
    diff = start - time_stamp;

#ifdef DEL_QEMU_OVERHEAD
    if(diff < devices[device_index].cell_program_delay){
        SSD_UPDATE_QEMU_OVERHEAD(device_index, devices[device_index].cell_program_delay-diff);
    }
    diff = start - __atomic_load_n(&ssds_manager[device_index].cell_io_time[reg], __ATOMIC_ACQUIRE);
#endif

    if( diff < devices[device_index].cell_program_delay){
        wait_usec(devices[device_index].cell_program_delay - diff);
        ret = 1;
    }

    /* Update Time Stamp Struct */
    __atomic_store_n(&ssds_manager[device_index].cell_io_time[reg], -1, __ATOMIC_RELEASE);
    __atomic_store_n(&ssds_manager[device_index].reg_io_type[reg], NOOP, __ATOMIC_RELEASE);

    return ret;
}

int SSD_CELL_READ_DELAY(uint8_t device_index, int reg)
{
    int ret = 0;
    int64_t start = 0;
    int64_t diff = 0;
    int64_t time_stamp = __atomic_load_n(&ssds_manager[device_index].cell_io_time[reg], __ATOMIC_ACQUIRE);

    int64_t REG_DELAY = devices[device_index].cell_read_delay;

    if (time_stamp == -1)
        return 0;

    /* Cell Read Delay */
    start = get_usec();
    diff = start - time_stamp;

#ifdef DEL_QEMU_OVERHEAD
    if( diff < REG_DELAY){
        SSD_UPDATE_QEMU_OVERHEAD(device_index, REG_DELAY-diff);
    }
    diff = start - __atomic_load_n(&ssds_manager[device_index].cell_io_time[reg], __ATOMIC_ACQUIRE);
#endif

    if( diff < REG_DELAY){
        wait_usec(REG_DELAY - diff);
        ret = 1;
    }

    /* Update Time Stamp Struct */
    __atomic_store_n(&ssds_manager[device_index].cell_io_time[reg], -1, __ATOMIC_RELEASE);
    __atomic_store_n(&ssds_manager[device_index].reg_io_type[reg], NOOP, __ATOMIC_RELEASE);

    return ret;
}

int SSD_BLOCK_ERASE_DELAY(uint8_t device_index, int reg)
{
    int ret = 0;
    int64_t diff;
    int64_t time_stamp = __atomic_load_n(&ssds_manager[device_index].cell_io_time[reg], __ATOMIC_ACQUIRE);

    if (time_stamp == -1)
        return 0;

    /* Block Erase Delay */
    diff = get_usec() - __atomic_load_n(&ssds_manager[device_index].cell_io_time[reg], __ATOMIC_ACQUIRE);
    if( diff < devices[device_index].block_erase_delay){
        wait_usec(devices[device_index].block_erase_delay - diff);
        ret = 1;
    }

    /* Update IO Overhead */
    __atomic_store_n(&ssds_manager[device_index].cell_io_time[reg], -1, __ATOMIC_RELEASE);
    __atomic_store_n(&ssds_manager[device_index].reg_io_cmd[reg], NOOP, __ATOMIC_RELEASE);
    __atomic_store_n(&ssds_manager[device_index].reg_io_type[reg], NOOP, __ATOMIC_RELEASE);

    return ret;
}

int64_t SSD_GET_CH_ACCESS_TIME_FOR_READ(uint8_t device_index, int channel, int reg)
{
    uint32_t i, j;
    uint32_t r_num;
    int64_t latest_time = __atomic_load_n(&ssds_manager[device_index].cell_io_time[reg], __ATOMIC_ACQUIRE) + devices[device_index].cell_read_delay;

    int64_t temp_time = 0;

    for(i=0;i<devices[device_index].way_nb;i++){
        r_num = channel*devices[device_index].planes_per_flash + i*devices[device_index].channel_nb*devices[device_index].planes_per_flash;
        for(j=0;j<devices[device_index].planes_per_flash;j++){
            temp_time = 0;

            if(__atomic_load_n(&ssds_manager[device_index].reg_io_cmd[r_num], __ATOMIC_ACQUIRE) == READ){
                temp_time = __atomic_load_n(&ssds_manager[device_index].reg_io_time[r_num], __ATOMIC_ACQUIRE) + devices[device_index].reg_read_delay;
            }
            else if(__atomic_load_n(&ssds_manager[device_index].reg_io_cmd[r_num], __ATOMIC_ACQUIRE) == WRITE){
                temp_time = __atomic_load_n(&ssds_manager[device_index].reg_io_time[r_num], __ATOMIC_ACQUIRE) + devices[device_index].reg_write_delay;
            }

            if( temp_time > latest_time ){
                latest_time = temp_time;
            }
            r_num++;
        }
    }

    return latest_time;
}

void SSD_UPDATE_CH_ACCESS_TIME(uint8_t device_index, int channel, int64_t current_time)
{
    uint32_t i, j;
    uint32_t r_num;

    for(i=0;i<devices[device_index].way_nb;i++){
        r_num = channel*devices[device_index].planes_per_flash + i*devices[device_index].channel_nb*devices[device_index].planes_per_flash;
        for(j=0;j<devices[device_index].planes_per_flash;j++){
            if(__atomic_load_n(&ssds_manager[device_index].reg_io_cmd[r_num], __ATOMIC_ACQUIRE) == READ && __atomic_load_n(&ssds_manager[device_index].reg_io_time[r_num], __ATOMIC_ACQUIRE) > current_time ){
                __atomic_fetch_add(&ssds_manager[device_index].reg_io_time[r_num], devices[device_index].reg_write_delay, __ATOMIC_ACQ_REL);
            }
            r_num++;
        }
    }
}

void SSD_REMAIN_IO_DELAY(uint8_t device_index, unsigned int flash_nb, int channel, int reg)
{
    SSD_REG_ACCESS(device_index, flash_nb, channel, reg);
}

//MIX
int64_t qemu_overhead;

void SSD_UPDATE_QEMU_OVERHEAD(uint8_t device_index, int64_t delay)
{
    int i;
    int p_num = devices[device_index].flash_nb * devices[device_index].planes_per_flash;
    int64_t diff = delay;
    int64_t qemu_overhead_val = __atomic_load_n(&qemu_overhead, __ATOMIC_ACQUIRE);

    if(qemu_overhead_val == 0){
        return;
    }
    else{
        if(diff > qemu_overhead_val){
            diff = qemu_overhead_val;
        }
    }

    __atomic_fetch_sub(&ssds_manager[device_index].last_operation_time_us, diff, __ATOMIC_ACQ_REL);
    for (i=0;i<p_num;i++){
        __atomic_fetch_sub(&ssds_manager[device_index].cell_io_time[i], diff, __ATOMIC_ACQ_REL);
        __atomic_fetch_sub(&ssds_manager[device_index].reg_io_time[i], diff, __ATOMIC_ACQ_REL);
    }
    __atomic_fetch_sub(&qemu_overhead, diff, __ATOMIC_ACQ_REL);
}

ftl_ret_val SSD_PAGE_COPYBACK(uint8_t device_index, uint32_t source, uint32_t destination, int type)
{
    uint32_t flash_nb, block_nb;
    uint32_t dest_flash_nb, dest_block_nb;
    uint32_t source_plane, destination_plane;
    uint32_t reg , channel;
    int delay_ret = 0;

    //Check source and destination pages are at the same plane.
    block_nb = CALC_BLOCK(device_index, source);
    source_plane = CALC_FLASH(device_index, source)*devices[device_index].planes_per_flash + block_nb%devices[device_index].planes_per_flash;
    destination_plane = CALC_FLASH(device_index, destination) * devices[device_index].planes_per_flash + block_nb%devices[device_index].planes_per_flash;
    if (source_plane != destination_plane){
        //copyback from different planes is not supported
        return FTL_FAILURE;
    }else{
        reg = destination_plane;
        flash_nb = CALC_FLASH(device_index, source);
    }

    channel = flash_nb % devices[device_index].channel_nb;
    __atomic_store_n(&ssds_manager[device_index].ssd.cur_channel_mode[channel], COPYBACK, __ATOMIC_RELEASE);

    int64_t start = get_usec();

    /* Delay Operation */
    //SSD_CH_ENABLE(flash_nb, channel);    // channel enable

    /* Access Register */
    if (devices[device_index].io_parallelism == 0 ){
        delay_ret = SSD_FLASH_ACCESS(device_index, flash_nb, channel);
    }
    else{
        delay_ret = SSD_REG_ACCESS(device_index, flash_nb, channel, reg);
    }

    SSD_CH_RECORD(device_index, channel, 0, delay_ret);
    SSD_CELL_RECORD(device_index, reg, channel);
    SSD_REG_RECORD(device_index, reg, type, channel);

    __atomic_fetch_add(&ssds_manager[device_index].ssd.occupied_pages_counter, 1, __ATOMIC_RELAXED);
    SSD_UTIL_LOG(device_index, flash_nb);
    __atomic_fetch_add(&ssds_manager[device_index].ssd.physical_page_writes, 1, __ATOMIC_RELAXED);

    dest_block_nb = CALC_BLOCK(device_index, destination);
    dest_flash_nb = CALC_FLASH(device_index, destination);
    inverse_block_mapping_entry* block_entry = GET_INVERSE_BLOCK_MAPPING_ENTRY(device_index, dest_flash_nb, dest_block_nb);
    block_entry->dirty_page_nb++;

    __atomic_store_n(&ssds_manager[device_index].ssd.prev_channel_mode[channel], COPYBACK, __ATOMIC_RELEASE);

    int64_t end = get_usec();

    LOG_PAGE_COPYBACK(GET_LOGGER(device_index, flash_nb), (PageCopyBackLog) {
        .channel = channel, .block = block_nb, .source_page = source, .destination_page = destination,
        .metadata = LOG_META(device_index, start, end),
        .background = (type == COPYBACK_BACKGROUND),
    });


    return FTL_SUCCESS;
}

double SSD_UTIL(uint8_t device_index) {
    const uint64_t total_pages    = (uint64_t)devices[device_index].pages_in_ssd;
    const uint64_t occupied_pages = __atomic_load_n(&ssds_manager[device_index].ssd.occupied_pages_counter, __ATOMIC_RELAXED);
    if (total_pages == 0) return 0.0;
    return (double)occupied_pages / (double)total_pages;
}

void SSD_UTIL_LOG(uint8_t device_index, unsigned flash_nb) {
    int64_t now = get_usec();

    const uint64_t total_pages    = (uint64_t)devices[device_index].pages_in_ssd;
    const uint64_t occupied_pages = __atomic_load_n(&ssds_manager[device_index].ssd.occupied_pages_counter, __ATOMIC_RELAXED);

    const double utilization = SSD_UTIL(device_index);

    // TODO: We are currently logging to flash_nb but the log is system wide
    // We might want to add a GET_SYSTEM_LOGGER() or a system_logger
    LOG_SSD_UTILIZATION(GET_LOGGER(device_index, flash_nb), (SsdUtilizationLog){
        .utilization_percent = utilization,
        .total_pages         = total_pages,
        .occupied_pages      = occupied_pages,
        .metadata            = LOG_META(device_index, now, now)
    });
}
