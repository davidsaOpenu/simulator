// Copyright(c)2013
//
// Hanyang University, Seoul, Korea
// Embedded Software Systems Lab. All right reserved

#include "common.h"

ssd_manager_t ssds_manager[MAX_DEVICES] = { 0 };

char ssd_version[4] = "1.0";
char ssd_date[9] = "13.04.11";

struct timeval logging_parser_tv;

int64_t time_delay = 0;

enum SSDTimeMode SSDTimeMode;

int64_t get_usec(void)
{
    int64_t t = 0;
    struct timeval tv;
    struct timezone tz;

    gettimeofday(&tv, &tz);
    t = tv.tv_sec;
    t *= 1000000;
    t += tv.tv_usec;

    t += time_delay;

    return t;
}

int SSD_IO_INIT(uint8_t device_index){

    uint32_t i= 0;

    /* Print SSD version */
    PINFO("SSD Emulator Version: %s ver. (%s)\n", ssd_version, ssd_date);

    /* Init Variable for Channel Switch Delay */
    ssds_manager[device_index].old_channel_nb = devices[device_index].channel_nb;
    ssds_manager[device_index].old_channel_time = 0;

    /* Init ssd statistic */
    ssds_manager[device_index].ssd.occupied_pages_counter = 0;
    ssds_manager[device_index].ssd.physical_page_writes = 0;
    ssds_manager[device_index].ssd.logical_page_writes = 0;

    /* Init Variable for Time-stamp */

    /* Init Command and Command type */
    ssds_manager[device_index].reg_io_cmd = (int *)malloc(sizeof(int) * devices[device_index].flash_nb * devices[device_index].planes_per_flash);
    for(i=0; i< devices[device_index].flash_nb*devices[device_index].planes_per_flash; i++){
        *(ssds_manager[device_index].reg_io_cmd + i) = NOOP;
    }

    ssds_manager[device_index].reg_io_type = (int *)malloc(sizeof(int) * devices[device_index].flash_nb * devices[device_index].planes_per_flash);
    for(i=0; i< devices[device_index].flash_nb*devices[device_index].planes_per_flash; i++){
        *(ssds_manager[device_index].reg_io_type + i) = NOOP;
    }

    /* Init Register and Flash IO Time */
    ssds_manager[device_index].reg_io_time = (int64_t *)malloc(sizeof(int64_t) * devices[device_index].flash_nb * devices[device_index].planes_per_flash);
    for(i=0; i<devices[device_index].flash_nb*devices[device_index].planes_per_flash; i++){
        *(ssds_manager[device_index].reg_io_time +i)= -1;
    }

    ssds_manager[device_index].cell_io_time = (int64_t *)malloc(sizeof(int64_t) * devices[device_index].flash_nb * devices[device_index].planes_per_flash);
    for(i=0; i< devices[device_index].flash_nb*devices[device_index].planes_per_flash; i++){
        *(ssds_manager[device_index].cell_io_time + i) = -1;
    }

    /* Init Access sequence_nb */
    ssds_manager[device_index].access_nb = (uint32_t **)malloc(sizeof(uint32_t*) * devices[device_index].flash_nb * devices[device_index].planes_per_flash);
    for(i=0; i< devices[device_index].flash_nb*devices[device_index].planes_per_flash; i++){
        *(ssds_manager[device_index].access_nb + i) = (uint32_t*)malloc(sizeof(uint32_t)*2);
        ssds_manager[device_index].access_nb[i][0] = UINT32_MAX;
        ssds_manager[device_index].access_nb[i][1] = UINT32_MAX;
    }

    /* Init IO Overhead */
    ssds_manager[device_index].io_overhead = (int64_t *)malloc(sizeof(int64_t) * devices[device_index].flash_nb * devices[device_index].planes_per_flash);
    for(i=0; i< devices[device_index].flash_nb*devices[device_index].planes_per_flash; i++){
        *(ssds_manager[device_index].io_overhead + i) = 0;
    }

    ssds_manager[device_index].ssd.prev_channel_mode = (int *)malloc(sizeof(int)*devices[device_index].channel_nb);
    ssds_manager[device_index].ssd.cur_channel_mode = (int *)malloc(sizeof(int)*devices[device_index].channel_nb);
    for (i = 0; i < devices[device_index].channel_nb; i++) {
        ssds_manager[device_index].ssd.prev_channel_mode[i] = NOOP;
        ssds_manager[device_index].ssd.cur_channel_mode[i] = NOOP;
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
    uint32_t i;
    for(i=0; i < devices[device_index].flash_nb * devices[device_index].planes_per_flash; i++)
        free(ssds_manager[device_index].access_nb[i]);
    free(ssds_manager[device_index].access_nb);
    free(ssds_manager[device_index].io_overhead);
    free(ssds_manager[device_index].ssd.prev_channel_mode);
    free(ssds_manager[device_index].ssd.cur_channel_mode);
    return 0;
}

static ftl_ret_val SSD_CELL_RECORD(uint8_t device_index, int reg, int channel)
{
    ftl_ret_val retval = FTL_SUCCESS;
    switch (ssds_manager[device_index].ssd.cur_channel_mode[channel]) {
        case WRITE:
            ssds_manager[device_index].cell_io_time[reg] = ssds_manager[device_index].old_channel_time + devices[device_index].reg_write_delay;
            break;
        case READ:
            ssds_manager[device_index].cell_io_time[reg] = ssds_manager[device_index].old_channel_time;
            break;
        case ERASE: // fallthrough
        case COPYBACK:
            ssds_manager[device_index].cell_io_time[reg] = get_usec();
            break;
        default:
            PERR("Unexpected current channel mode = %d\n", ssds_manager[device_index].ssd.cur_channel_mode[channel])
            retval = FTL_FAILURE;
    }

    return retval;
}

static int SSD_CH_RECORD(uint8_t device_index, int channel, int offset, int ret)
{
    if (ssds_manager[device_index].ssd.prev_channel_mode[channel] == READ && offset != 0 && ret == 0){
        ssds_manager[device_index].old_channel_time += devices[device_index].channel_switch_delay_w;
    }
    else if (ssds_manager[device_index].ssd.prev_channel_mode[channel] == WRITE && offset != 0 && ret == 0) {
        ssds_manager[device_index].old_channel_time += devices[device_index].channel_switch_delay_r;
    }
    else {
        ssds_manager[device_index].old_channel_time = get_usec();
    }
    return FTL_SUCCESS;
}

ftl_ret_val SSD_PAGE_WRITE(uint8_t device_index, unsigned int flash_nb, unsigned int block_nb, unsigned int page_nb, int offset, int type)
{
    uint32_t channel, reg;
    int ret = FTL_FAILURE;
    int delay_ret;

    GET_TIME_MICROSEC(_start);

    /* Calculate ch & reg */
    channel = flash_nb % devices[device_index].channel_nb;
    ssds_manager[device_index].ssd.cur_channel_mode[channel] = WRITE;
    reg = flash_nb*devices[device_index].planes_per_flash + block_nb%devices[device_index].planes_per_flash;

    /* Delay Operation */
    SSD_CH_ENABLE(device_index, flash_nb, channel);    // channel enable

    if( devices[device_index].io_parallelism == 0 ){
        delay_ret = SSD_FLASH_ACCESS(device_index, flash_nb, channel, reg);
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
    SSD_REG_RECORD(device_index, reg, type, offset, channel);
    SSD_REG_ACCESS(device_index, flash_nb, channel, reg);

    GET_TIME_MICROSEC(_end);
    if (ssds_manager[device_index].old_channel_nb == channel && ssds_manager[device_index].ssd.prev_channel_mode[channel] != WRITE) { //if channel is same but only mode is different
        // PINFO("change to write for channel %d\n", channel);
        LOG_CHANNEL_SWITCH_TO_WRITE(GET_LOGGER(device_index, flash_nb), (ChannelSwitchToWriteLog) {
            .channel = channel,
            .metadata = {_start, _end}
        });
    }
    ssds_manager[device_index].ssd.prev_channel_mode[channel] = WRITE;
    ssds_manager[device_index].old_channel_nb = channel;

    /* Update ssd page write counters */
    if (type != WRITE_COMMIT) {
        ssds_manager[device_index].ssd.occupied_pages_counter++;
    }
    ssds_manager[device_index].ssd.physical_page_writes++;

    inverse_block_mapping_entry* block_entry = GET_INVERSE_BLOCK_MAPPING_ENTRY(device_index, flash_nb, block_nb);
    block_entry->dirty_page_nb++;

    if (type == WRITE_COMMIT) {
        LOG_PHYSICAL_CELL_PROGRAM_COMPATIBLE(GET_LOGGER(device_index, flash_nb), (PhysicalCellProgramCompatibleLog) {
            .channel = channel, .block = block_nb, .page = page_nb,
            .metadata = {_start, _end}
        });
    }
    else {
        LOG_PHYSICAL_CELL_PROGRAM(GET_LOGGER(device_index, flash_nb), (PhysicalCellProgramLog) {
            .channel = channel, .block = block_nb, .page = page_nb,
            .metadata = {_start, _end}
        });
    }


    if (type == WRITE || type == WRITE_COMMIT) { // if we log logical write first, write amp may get negative
        ssds_manager[device_index].ssd.logical_page_writes++;

        LOG_LOGICAL_CELL_PROGRAM(GET_LOGGER(device_index, flash_nb),(LogicalCellProgramLog) {
            .channel = channel, .block = block_nb, .page = page_nb,
            .metadata = {_start, _end}
        });
    }

    return ret;
}

ftl_ret_val SSD_PAGE_READ(uint8_t device_index, unsigned int flash_nb, unsigned int block_nb, unsigned int page_nb, int offset, int type)
{
    unsigned int channel, reg;
    int delay_ret;

    GET_TIME_MICROSEC(_start);

    /* Calculate ch & reg */
    channel = flash_nb % devices[device_index].channel_nb;
    ssds_manager[device_index].ssd.cur_channel_mode[channel] = READ;
    reg = flash_nb*devices[device_index].planes_per_flash + block_nb%devices[device_index].planes_per_flash;

    /* Delay Operation */
    SSD_CH_ENABLE(device_index, flash_nb, channel);    // channel enable

    /* Access Register */
    if( devices[device_index].io_parallelism == 0 ){
        delay_ret = SSD_FLASH_ACCESS(device_index, flash_nb, channel, reg);
    }
    else{
        delay_ret = SSD_REG_ACCESS(device_index, flash_nb, channel, reg);
    }

    /* Record Time Stamp */
    SSD_CH_RECORD(device_index, channel, offset, delay_ret);
    SSD_CELL_RECORD(device_index, reg, channel);
    SSD_REG_RECORD(device_index, reg, type, offset, channel);
    SSD_REG_ACCESS(device_index, flash_nb, channel, reg);

    GET_TIME_MICROSEC(_end);

    if (ssds_manager[device_index].old_channel_nb == channel && ssds_manager[device_index].ssd.prev_channel_mode[channel] != READ && ssds_manager[device_index].ssd.prev_channel_mode[channel] != NOOP) {
        LOG_CHANNEL_SWITCH_TO_READ(GET_LOGGER(device_index, flash_nb), (ChannelSwitchToReadLog) {
            .channel = channel,
            .metadata = {_start, _end}
        });
    }
    ssds_manager[device_index].ssd.prev_channel_mode[channel] = READ;
    ssds_manager[device_index].old_channel_nb = channel;

    LOG_PHYSICAL_CELL_READ(GET_LOGGER(device_index, flash_nb), (PhysicalCellReadLog) {
        .channel = channel, .block = block_nb, .page = page_nb,
        .metadata = {_start, _end}
    });

    return FTL_SUCCESS;
}

ftl_ret_val SSD_BLOCK_ERASE(uint8_t device_index, unsigned int flash_nb, unsigned int block_nb)
{
    int channel, reg;

    GET_TIME_MICROSEC(_start);

    /* Calculate ch & reg */
    channel = flash_nb % devices[device_index].channel_nb;
    ssds_manager[device_index].ssd.cur_channel_mode[channel] = ERASE;
    reg = flash_nb*devices[device_index].planes_per_flash + block_nb%devices[device_index].planes_per_flash;

    /* Delay Operation */
    if( devices[device_index].io_parallelism == 0 ){
        SSD_FLASH_ACCESS(device_index, flash_nb, channel, reg);
    }
    else{
        SSD_REG_ACCESS(device_index, flash_nb, channel, reg);
    }

    /* Record Time Stamp */
    SSD_REG_RECORD(device_index, reg, ERASE, -1, channel);
    SSD_CELL_RECORD(device_index, reg, channel);

    GET_TIME_MICROSEC(_end);

    inverse_block_mapping_entry* block_entry = GET_INVERSE_BLOCK_MAPPING_ENTRY(device_index, flash_nb, block_nb);

    ssds_manager[device_index].ssd.occupied_pages_counter -= block_entry->dirty_page_nb;
    ssds_manager[device_index].ssd.prev_channel_mode[channel] = ERASE;

    LOG_BLOCK_ERASE(GET_LOGGER(device_index, flash_nb), (BlockEraseLog) {
        .channel = channel, .die = flash_nb, .block = block_nb, .dirty_page_nb = block_entry->dirty_page_nb,
        .metadata = {_start, _end}
    });

    block_entry->dirty_page_nb = 0;

    return FTL_SUCCESS;
}

int SSD_FLASH_ACCESS(uint8_t device_index, unsigned int flash_nb, unsigned int channel, unsigned int reg)
{
    uint32_t i;
    uint32_t r_num = flash_nb * devices[device_index].planes_per_flash;
    int ret = 0;

    for (i=0;i<devices[device_index].planes_per_flash;i++) {

        if (r_num != reg && ssds_manager[device_index].access_nb[r_num][0] == io_request_seq_nb) {
            /* That's OK */
        }
        else{
            ret = SSD_REG_ACCESS(device_index, flash_nb, channel, r_num);
        }

        r_num++;
    }

    return ret;
}

int SSD_REG_ACCESS(uint8_t device_index, unsigned int flash_nb, int channel, int reg)
{
    switch (ssds_manager[device_index].reg_io_cmd[reg]){
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
            PERR("SSD_REG_ACCESS: Command Error! %d\n", ssds_manager[device_index].reg_io_cmd[reg]);
            return 0;
    }
}

int SSD_CH_ENABLE(uint8_t device_index, unsigned int flash_nb, unsigned int channel)
{
    if(devices[device_index].channel_switch_delay_r == 0 && devices[device_index].channel_switch_delay_w == 0)
        return FTL_SUCCESS;

        //todo: currently writing on all channels at the same time takes more time than writing on one
    if(ssds_manager[device_index].old_channel_nb != channel){
        SSD_CH_SWITCH_DELAY(device_index, flash_nb, channel);
    }

    return FTL_SUCCESS;
}

ftl_ret_val SSD_REG_RECORD(uint8_t device_index, int reg, int type, int offset, int channel)
{
    ssds_manager[device_index].reg_io_cmd[reg] = ssds_manager[device_index].ssd.cur_channel_mode[channel];
    ssds_manager[device_index].reg_io_type[reg] = type;
    ftl_ret_val retval = FTL_SUCCESS;

    switch (ssds_manager[device_index].reg_io_cmd[reg]) {
        case WRITE:
            ssds_manager[device_index].reg_io_time[reg] = ssds_manager[device_index].old_channel_time;
            if (ssds_manager[device_index].ssd.prev_channel_mode[channel] == READ) {
                ssds_manager[device_index].reg_io_time[reg] += devices[device_index].channel_switch_delay_w;
            }
            // SSD_UPDATE_CH_ACCESS_TIME(channel, ssds_manager[device_index].reg_io_time[reg]);

            /* Update SATA request Info */
            if(type == WRITE) {
                ssds_manager[device_index].access_nb[reg][0] = io_request_seq_nb;
                ssds_manager[device_index].access_nb[reg][1] = offset;
                ssds_manager[device_index].io_update_overhead = UPDATE_IO_REQUEST(device_index, io_request_seq_nb, offset, ssds_manager[device_index].old_channel_time, UPDATE_START_TIME);
                SSD_UPDATE_IO_OVERHEAD(device_index, reg, ssds_manager[device_index].io_update_overhead);
            }
            else{
                ssds_manager[device_index].access_nb[reg][0] = UINT32_MAX;
                ssds_manager[device_index].access_nb[reg][1] = UINT32_MAX;
            }
        break;
    case READ:
        ssds_manager[device_index].reg_io_time[reg] = SSD_GET_CH_ACCESS_TIME_FOR_READ(device_index, channel, reg);
        if (ssds_manager[device_index].ssd.prev_channel_mode[channel] != READ && ssds_manager[device_index].ssd.prev_channel_mode[channel] != NOOP) {
                ssds_manager[device_index].reg_io_time[reg] += devices[device_index].channel_switch_delay_r;
            }
            /* Update SATA request Info */
            if(type == READ){
                ssds_manager[device_index].access_nb[reg][0] = io_request_seq_nb;
                ssds_manager[device_index].access_nb[reg][1] = offset;
                ssds_manager[device_index].io_update_overhead = UPDATE_IO_REQUEST(device_index, io_request_seq_nb, offset, ssds_manager[device_index].old_channel_time, UPDATE_START_TIME);
                SSD_UPDATE_IO_OVERHEAD(device_index, reg, ssds_manager[device_index].io_update_overhead);
            }
            else{
                ssds_manager[device_index].access_nb[reg][0] = UINT32_MAX;
                ssds_manager[device_index].access_nb[reg][1] = UINT32_MAX;
            }
        break;
    case ERASE:
        /* Update SATA request Info */
        ssds_manager[device_index].access_nb[reg][0] = UINT32_MAX;
        ssds_manager[device_index].access_nb[reg][1] = UINT32_MAX;
        break;
    case COPYBACK:
        ssds_manager[device_index].reg_io_time[reg] = ssds_manager[device_index].cell_io_time[reg] + devices[device_index].cell_read_delay;
        ssds_manager[device_index].access_nb[reg][0] = io_request_seq_nb;
        ssds_manager[device_index].access_nb[reg][1] = offset;
        ssds_manager[device_index].io_update_overhead = UPDATE_IO_REQUEST(device_index, io_request_seq_nb, offset, ssds_manager[device_index].old_channel_time, UPDATE_START_TIME);
        SSD_UPDATE_IO_OVERHEAD(device_index, reg, ssds_manager[device_index].io_update_overhead);
        break;
    default:
        PERR("SSD_REG_RECORD: Command Error! %d\n", ssds_manager[device_index].reg_io_cmd[reg]);
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
            if(ssds_manager[device_index].reg_io_time[r_num] <= get_usec() && ssds_manager[device_index].reg_io_time[r_num] != -1){
                if(ssds_manager[device_index].reg_io_cmd[r_num] == READ){
                    SSD_CELL_READ_DELAY(device_index, r_num);
                    SSD_REG_READ_DELAY(device_index, flash_nb, channel, r_num);
                    ret = FTL_FAILURE;
                }
                else if(ssds_manager[device_index].reg_io_cmd[r_num] == WRITE){
                    SSD_REG_WRITE_DELAY(device_index, flash_nb, channel, r_num);
                    ret = FTL_FAILURE;
                }
            }
            r_num++;
        }
    }

    return ret;
}

void SSD_UPDATE_IO_OVERHEAD(uint8_t device_index, int reg, int64_t overhead_time)
{
    ssds_manager[device_index].io_overhead[reg] += overhead_time;
    ssds_manager[device_index].io_alloc_overhead = 0;
    ssds_manager[device_index].io_update_overhead = 0;
}

int64_t SSD_CH_SWITCH_DELAY(uint8_t device_index, unsigned int flash_nb, int channel)
{
    int64_t start = 0;
    int64_t    end = 0;
    int64_t diff = 0;
    int64_t switch_delay = 0;

    GET_TIME_MICROSEC(_start);
    if (ssds_manager[device_index].ssd.cur_channel_mode[channel] == READ ) {
        switch_delay = devices[device_index].channel_switch_delay_r;
    }
    else if (ssds_manager[device_index].ssd.cur_channel_mode[channel] == WRITE) {
        switch_delay = devices[device_index].channel_switch_delay_w;
    }
    else{
        return 0;
    }

    start = get_usec();
    diff = start - ssds_manager[device_index].old_channel_time;

#ifdef DEL_QEMU_OVERHEAD
    if(diff < switch_delay){
        SSD_UPDATE_QEMU_OVERHEAD(device_index, switch_delay-diff);
    }
#endif

    if (diff < switch_delay){
        wait_usec(switch_delay - diff);
    }

    GET_TIME_MICROSEC(_end);

    switch(ssds_manager[device_index].ssd.cur_channel_mode[channel]){
        case READ:{
            LOG_CHANNEL_SWITCH_TO_READ(GET_LOGGER(device_index, flash_nb), (ChannelSwitchToReadLog) {
                .channel = channel,
                .metadata = {_start, _end}
            });
            break;
        }

        case WRITE:{
            // PINFO("write first write to channel %d\n", channel);
            LOG_CHANNEL_SWITCH_TO_WRITE(GET_LOGGER(device_index, flash_nb), (ChannelSwitchToWriteLog) {
                .channel = channel,
                .metadata = {_start, _end}
            });
            break;
        }

        case COPYBACK:{
            //TODO: log channel switch to copyback
            break;
        }

        default:{
            RERR(end - start, "New Channel mode unexpected : %d", ssds_manager[device_index].ssd.cur_channel_mode[channel]);
            break;
        }
    }
    return end-start;
}

int SSD_REG_WRITE_DELAY(uint8_t device_index, unsigned int flash_nb, int channel, int reg)
{
    int ret = 0;
    int64_t start = 0;
    int64_t diff = 0;
    int64_t time_stamp = ssds_manager[device_index].reg_io_time[reg];

    GET_TIME_MICROSEC(_start);

    if (time_stamp == -1)
        return 0;

    /* Reg Write Delay */
    start = get_usec();
    diff = start - time_stamp;

#ifdef DEL_QEMU_OVERHEAD
    if(diff < devices[device_index].reg_write_delay){
        SSD_UPDATE_QEMU_OVERHEAD(device_index, devices[device_index].reg_write_delay-diff);
    }
    diff = start - ssds_manager[device_index].reg_io_time[reg];
#endif

    int64_t delay = 0;
    if (diff < devices[device_index].reg_write_delay){
        wait_usec(devices[device_index].reg_write_delay - diff);
        delay = devices[device_index].reg_write_delay - diff;
        ret = 1;
    }

    /* Send Delay Info To Perf Checker */
    SEND_TO_PERF_CHECKER(device_index, ssds_manager[device_index].reg_io_type[reg], delay, CH_OP);

    /* Update Time Stamp Struct */
    ssds_manager[device_index].reg_io_time[reg] = -1;
    ssds_manager[device_index].reg_io_cmd[reg] = NOOP;

    GET_TIME_MICROSEC(_end);

    LOG_REGISTER_WRITE(GET_LOGGER(device_index, flash_nb), (RegisterWriteLog) {
        .channel = channel, .die = flash_nb, .reg = reg,
        .metadata = {_start, _end}
    });

    return ret;
}

int SSD_REG_READ_DELAY(uint8_t device_index, unsigned int flash_nb, int channel, int reg)
{
    int ret = 0;
    int64_t start = 0;
    int64_t end = 0;
    int64_t diff = 0;
    int64_t time_stamp = ssds_manager[device_index].reg_io_time[reg];
    GET_TIME_MICROSEC(_start);
    if (time_stamp == -1)
        return 0;

    /* Reg Read Delay */
    start = get_usec();
    diff = start - time_stamp;

#ifdef DEL_QEMU_OVERHEAD
    if(diff < devices[device_index].reg_read_delay){
        SSD_UPDATE_QEMU_OVERHEAD(device_index, devices[device_index].reg_read_delay - diff);
    }
    diff = start - ssds_manager[device_index].reg_io_time[reg];
#endif

    if(diff < devices[device_index].reg_read_delay){
        wait_usec(devices[device_index].reg_read_delay - diff);
        ret = 1;
    }
    end = get_usec();

    /* Send Delay Info To Perf Checker */
    SEND_TO_PERF_CHECKER(device_index, ssds_manager[device_index].reg_io_type[reg], end-start, CH_OP);
    SSD_UPDATE_IO_REQUEST(device_index, reg);

    /* Update Time Stamp Struct */
    ssds_manager[device_index].reg_io_time[reg] = -1;
    ssds_manager[device_index].reg_io_cmd[reg] = NOOP;

    GET_TIME_MICROSEC(_end);

    LOG_REGISTER_READ(GET_LOGGER(device_index, flash_nb), (RegisterReadLog) {
        .channel = channel, .die = flash_nb, .reg = reg,
        .metadata = {_start, _end}
    });

    return ret;
}

int SSD_CELL_WRITE_DELAY(uint8_t device_index, int reg)
{
    int ret = 0;
    int64_t start = 0;
    int64_t diff = 0;
    int64_t time_stamp = ssds_manager[device_index].cell_io_time[reg];

    if( time_stamp == -1 )
        return 0;

    /* Cell Write Delay */
    start = get_usec();
    diff = start - time_stamp + ssds_manager[device_index].io_overhead[reg];

#ifdef DEL_QEMU_OVERHEAD
    if(diff < devices[device_index].cell_program_delay){
        SSD_UPDATE_QEMU_OVERHEAD(device_index, devices[device_index].cell_program_delay-diff);
    }
    diff = start - ssds_manager[device_index].cell_io_time[reg] + ssds_manager[device_index].io_overhead[reg];
#endif

    if( diff < devices[device_index].cell_program_delay){
        ssds_manager[device_index].init_diff_reg = diff;
        wait_usec(devices[device_index].cell_program_delay - diff);
        ret = 1;
    }

    /* Send Delay Info To Perf Checker */
    SEND_TO_PERF_CHECKER(device_index, ssds_manager[device_index].reg_io_type[reg], diff, REG_OP);
    SSD_UPDATE_IO_REQUEST(device_index, reg);

    /* Update Time Stamp Struct */
    ssds_manager[device_index].cell_io_time[reg] = -1;
    ssds_manager[device_index].reg_io_type[reg] = NOOP;

    /* Update IO Overhead */
    ssds_manager[device_index].io_overhead[reg] = 0;

    return ret;
}

int SSD_CELL_READ_DELAY(uint8_t device_index, int reg)
{
    int ret = 0;
    int64_t start = 0;
    int64_t end = 0;
    int64_t diff = 0;
    int64_t time_stamp = ssds_manager[device_index].cell_io_time[reg];

    int64_t REG_DELAY = devices[device_index].cell_read_delay;

    if( time_stamp == -1)
        return 0;

    /* Cell Read Delay */
    start = get_usec();
    diff = start - time_stamp + ssds_manager[device_index].io_overhead[reg];

#ifdef DEL_QEMU_OVERHEAD
    if( diff < REG_DELAY){
        SSD_UPDATE_QEMU_OVERHEAD(device_index, REG_DELAY-diff);
    }
    diff = start - ssds_manager[device_index].cell_io_time[reg] + ssds_manager[device_index].io_overhead[reg];
#endif

    if( diff < REG_DELAY){
        ssds_manager[device_index].init_diff_reg = diff;
        wait_usec(REG_DELAY - diff);
        ret = 1;

    }
    end = get_usec();

    /* Send Delay Info To Perf Checker */
    SEND_TO_PERF_CHECKER(device_index, ssds_manager[device_index].reg_io_type[reg], end-start, REG_OP);

    /* Update Time Stamp Struct */
    ssds_manager[device_index].cell_io_time[reg] = -1;
    ssds_manager[device_index].reg_io_type[reg] = NOOP;

    /* Update IO Overhead */
    ssds_manager[device_index].io_overhead[reg] = 0;

    return ret;
}

int SSD_BLOCK_ERASE_DELAY(uint8_t device_index, int reg)
{
    int ret = 0;
    int64_t diff;
    int64_t time_stamp = ssds_manager[device_index].cell_io_time[reg];

    if( time_stamp == -1)
        return 0;

    /* Block Erase Delay */
    diff = get_usec() - ssds_manager[device_index].cell_io_time[reg];
    if( diff < devices[device_index].block_erase_delay){
        wait_usec(devices[device_index].block_erase_delay - diff);
        ret = 1;
    }

    /* Update IO Overhead */
    ssds_manager[device_index].cell_io_time[reg] = -1;
    ssds_manager[device_index].reg_io_cmd[reg] = NOOP;
    ssds_manager[device_index].reg_io_type[reg] = NOOP;

    return ret;
}

int64_t SSD_GET_CH_ACCESS_TIME_FOR_READ(uint8_t device_index, int channel, int reg)
{
    uint32_t i, j;
    uint32_t r_num;
    int64_t latest_time = ssds_manager[device_index].cell_io_time[reg] + devices[device_index].cell_read_delay;

    int64_t temp_time = 0;

    for(i=0;i<devices[device_index].way_nb;i++){
        r_num = channel*devices[device_index].planes_per_flash + i*devices[device_index].channel_nb*devices[device_index].planes_per_flash;
        for(j=0;j<devices[device_index].planes_per_flash;j++){
            temp_time = 0;

            if(ssds_manager[device_index].reg_io_cmd[r_num] == READ){
                temp_time = ssds_manager[device_index].reg_io_time[r_num] + devices[device_index].reg_read_delay;
            }
            else if(ssds_manager[device_index].reg_io_cmd[r_num] == WRITE){
                temp_time = ssds_manager[device_index].reg_io_time[r_num] + devices[device_index].reg_write_delay;
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
            if(ssds_manager[device_index].reg_io_cmd[r_num] == READ && ssds_manager[device_index].reg_io_time[r_num] > current_time ){
                ssds_manager[device_index].reg_io_time[r_num] += devices[device_index].reg_write_delay;
            }
            r_num++;
        }
    }
}

void SSD_UPDATE_IO_REQUEST(uint8_t device_index, int reg)
{
    int64_t curr_time = get_usec();
    if(ssds_manager[device_index].init_diff_reg != 0){
        ssds_manager[device_index].io_update_overhead = UPDATE_IO_REQUEST(device_index, ssds_manager[device_index].access_nb[reg][0], ssds_manager[device_index].access_nb[reg][1], curr_time, UPDATE_END_TIME);
        SSD_UPDATE_IO_OVERHEAD(device_index, reg, ssds_manager[device_index].io_update_overhead);
        ssds_manager[device_index].access_nb[reg][0] = UINT32_MAX;
    }
    else{
        ssds_manager[device_index].io_update_overhead = UPDATE_IO_REQUEST(device_index, ssds_manager[device_index].access_nb[reg][0], ssds_manager[device_index].access_nb[reg][1], 0, UPDATE_END_TIME);
        SSD_UPDATE_IO_OVERHEAD(device_index, reg, ssds_manager[device_index].io_update_overhead);
        ssds_manager[device_index].access_nb[reg][0] = UINT32_MAX;
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

    if(qemu_overhead == 0){
        return;
    }
    else{
        if(diff > qemu_overhead){
            diff = qemu_overhead;
        }
    }

    ssds_manager[device_index].old_channel_time -= diff;
    for(i=0;i<p_num;i++){
        ssds_manager[device_index].cell_io_time[i] -= diff;
        ssds_manager[device_index].reg_io_time[i] -= diff;
    }
    qemu_overhead -= diff;
}

ftl_ret_val SSD_PAGE_COPYBACK(uint8_t device_index, uint32_t source, uint32_t destination, int type){

    uint32_t flash_nb, block_nb;
    uint32_t dest_flash_nb, dest_block_nb;
    uint32_t source_plane, destination_plane;
    uint32_t reg , channel , delay_ret;

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
    ssds_manager[device_index].ssd.cur_channel_mode[channel] = COPYBACK;

    GET_TIME_MICROSEC(_start);

    /* Delay Operation */
    //SSD_CH_ENABLE(flash_nb, channel);    // channel enable

    /* Access Register */
    if( devices[device_index].io_parallelism == 0 ){
        delay_ret = SSD_FLASH_ACCESS(device_index, flash_nb, channel, reg);
    }
    else{
        delay_ret = SSD_REG_ACCESS(device_index, flash_nb, channel, reg);
    }

    SSD_CH_RECORD(device_index, channel, 0, delay_ret);
    SSD_CELL_RECORD(device_index, reg, channel);
    SSD_REG_RECORD(device_index, reg, type, 0, channel);

    ssds_manager[device_index].ssd.occupied_pages_counter++;
    ssds_manager[device_index].ssd.physical_page_writes++;

    dest_block_nb = CALC_BLOCK(device_index, destination);
    dest_flash_nb = CALC_FLASH(device_index, destination);
    inverse_block_mapping_entry* block_entry = GET_INVERSE_BLOCK_MAPPING_ENTRY(device_index, dest_flash_nb, dest_block_nb);
    block_entry->dirty_page_nb++;

    ssds_manager[device_index].ssd.prev_channel_mode[channel] = COPYBACK;

    GET_TIME_MICROSEC(_end);

    LOG_PAGE_COPYBACK(GET_LOGGER(device_index, flash_nb), (PageCopyBackLog) {
        .channel = channel, .block = block_nb, .source_page = source, .destination_page = destination,
        .metadata = {_start, _end}
    });


    return FTL_SUCCESS;
}

double SSD_UTIL(uint8_t device_index) {
    return (double)ssds_manager[device_index].ssd.occupied_pages_counter / devices[device_index].pages_in_ssd;
}
