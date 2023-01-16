// Copyright(c)2013
//
// Hanyang University, Seoul, Korea
// Embedded Software Systems Lab. All right reserved

#include "common.h"

int* reg_io_cmd;    // READ, WRITE, ERASE
int* reg_io_type;    // SEQ, RAN, MERGE, GC, etc..

int64_t* reg_io_time;
int64_t* cell_io_time;

uint32_t** access_nb;
int64_t* io_overhead;

int old_channel_nb;
int64_t old_channel_time;

int64_t init_diff_reg=0;

int64_t io_alloc_overhead=0;
int64_t io_update_overhead=0;

char ssd_version[4] = "1.0";
char ssd_date[9] = "13.04.11";

struct timeval logging_parser_tv;

ssd_disk ssd;

int64_t get_usec(void)
{
    int64_t t = 0;
    struct timeval tv;
    struct timezone tz;

    gettimeofday(&tv, &tz);
    t = tv.tv_sec;
    t *= 1000000;
    t += tv.tv_usec;

    return t;
}

int SSD_IO_INIT(void){

    uint32_t i= 0;

    /* Print SSD version */
    PINFO("SSD Emulator Version: %s ver. (%s)\n", ssd_version, ssd_date);

    /* Init Variable for Channel Switch Delay */
    old_channel_nb = CHANNEL_NB;
    old_channel_time = 0;

    /* Init ssd statistic */
    ssd.occupied_pages_counter = 0;
    ssd.physical_page_writes = 0;
    ssd.logical_page_writes = 0;

    /* Init Variable for Time-stamp */

    /* Init Command and Command type */
    reg_io_cmd = (int *)malloc(sizeof(int) * FLASH_NB * PLANES_PER_FLASH);
    for(i=0; i< FLASH_NB*PLANES_PER_FLASH; i++){
        *(reg_io_cmd + i) = NOOP;
    }

    reg_io_type = (int *)malloc(sizeof(int) * FLASH_NB * PLANES_PER_FLASH);
    for(i=0; i< FLASH_NB*PLANES_PER_FLASH; i++){
        *(reg_io_type + i) = NOOP;
    }

    /* Init Register and Flash IO Time */
    reg_io_time = (int64_t *)malloc(sizeof(int64_t) * FLASH_NB * PLANES_PER_FLASH);
    for(i=0; i<FLASH_NB*PLANES_PER_FLASH; i++){
        *(reg_io_time +i)= -1;
    }

    cell_io_time = (int64_t *)malloc(sizeof(int64_t) * FLASH_NB * PLANES_PER_FLASH);
    for(i=0; i< FLASH_NB*PLANES_PER_FLASH; i++){
        *(cell_io_time + i) = -1;
    }

    /* Init Access sequence_nb */
    access_nb = (uint32_t **)malloc(sizeof(uint32_t*) * FLASH_NB * PLANES_PER_FLASH);
    for(i=0; i< FLASH_NB*PLANES_PER_FLASH; i++){
        *(access_nb + i) = (uint32_t*)malloc(sizeof(uint32_t)*2);
        access_nb[i][0] = UINT32_MAX;
        access_nb[i][1] = UINT32_MAX;
    }

    /* Init IO Overhead */
    io_overhead = (int64_t *)malloc(sizeof(int64_t) * FLASH_NB * PLANES_PER_FLASH);
    for(i=0; i< FLASH_NB*PLANES_PER_FLASH; i++){
        *(io_overhead + i) = 0;
    }

        ssd.prev_channel_mode = (int *)malloc(sizeof(int)*CHANNEL_NB);
        ssd.cur_channel_mode = (int *)malloc(sizeof(int)*CHANNEL_NB);
        for (i = 0; i < CHANNEL_NB; i++) {
            ssd.prev_channel_mode[i] = NOOP;
            ssd.cur_channel_mode[i] = NOOP;
        }
        return 0;
}

int SSD_IO_TERM(void)
{
    free(reg_io_cmd);
    free(reg_io_type);
    free(reg_io_time);
    free(cell_io_time);
    uint32_t i;
    for(i=0; i< FLASH_NB*PLANES_PER_FLASH; i++)
        free(access_nb[i]);
    free(access_nb);
    free(io_overhead);
    free(ssd.prev_channel_mode);
    free(ssd.cur_channel_mode);
    return 0;
}

static ftl_ret_val SSD_CELL_RECORD(int reg, int channel)
{
    ftl_ret_val retval = FTL_SUCCESS;
    switch (ssd.cur_channel_mode[channel]) {
        case WRITE:
            cell_io_time[reg] = old_channel_time + REG_WRITE_DELAY;
            break;
        case READ:
            cell_io_time[reg] = old_channel_time;
            break;
        case ERASE: // fallthrough
        case COPYBACK:
            cell_io_time[reg] = get_usec();
            break;
        default:
            PERR("Unexpected current channel mode = %d\n", ssd.cur_channel_mode[channel])
            retval = FTL_FAILURE;
    }

    return retval;
}

static int SSD_CH_RECORD(int channel, int offset, int ret)
{
    old_channel_nb = channel;
    if (ssd.prev_channel_mode[channel] == READ && offset != 0 && ret == 0){
        old_channel_time += CHANNEL_SWITCH_DELAY_W;
    }
    else if (ssd.prev_channel_mode[channel] == WRITE && offset != 0 && ret == 0) {
        old_channel_time += CHANNEL_SWITCH_DELAY_R;
    }
    else {
        old_channel_time = get_usec();
    }
    return FTL_SUCCESS;
}

ftl_ret_val SSD_PAGE_WRITE(unsigned int flash_nb, unsigned int block_nb, unsigned int page_nb, int offset, int type)
{
    uint32_t channel, reg;
    int ret = FTL_FAILURE;
    int delay_ret;

    TIME_MICROSEC(_start);

    /* Calculate ch & reg */
    channel = flash_nb % CHANNEL_NB;
    ssd.cur_channel_mode[channel] = WRITE;
    reg = flash_nb*PLANES_PER_FLASH + block_nb%PLANES_PER_FLASH;

    /* Delay Operation */
    SSD_CH_ENABLE(flash_nb, channel);    // channel enable

    if( IO_PARALLELISM == 0 ){
        delay_ret = SSD_FLASH_ACCESS(flash_nb, channel, reg);
    }
    else{
        delay_ret = SSD_REG_ACCESS(flash_nb, channel, reg);
    }

    /* Check Channel Operation */
    while(ret == FTL_FAILURE){
        ret = SSD_CH_ACCESS(flash_nb, channel);
    }

    /* Record Time Stamp */
    SSD_CH_RECORD(channel, offset, delay_ret);
    SSD_CELL_RECORD(reg, channel);
    SSD_REG_RECORD(reg, type, offset, channel);
    SSD_REG_ACCESS(flash_nb, channel, reg);

    TIME_MICROSEC(_end);
    if (ssd.prev_channel_mode[channel] == READ) {
        LOG_CHANNEL_SWITCH_TO_WRITE(GET_LOGGER(flash_nb), (ChannelSwitchToWriteLog) {
            .channel = channel,
            .metadata = {_start, _end}
        });
    }
    ssd.prev_channel_mode[channel] = WRITE;

    /* Update ssd page write counters */
    ssd.occupied_pages_counter++;
    ssd.physical_page_writes++;
    if (type == WRITE) {
        ssd.logical_page_writes++;

        LOG_LOGICAL_CELL_PROGRAM(GET_LOGGER(flash_nb),(LogicalCellProgramLog) {
            .channel = channel, .block = block_nb, .page = page_nb,
            .metadata = {_start, _end}
        });
    }

    LOG_PHYSICAL_CELL_PROGRAM(GET_LOGGER(flash_nb), (PhysicalCellProgramLog) {
        .channel = channel, .block = block_nb, .page = page_nb,
        .metadata = {_start, _end}
    });

    return ret;
}

ftl_ret_val SSD_PAGE_READ(unsigned int flash_nb, unsigned int block_nb, unsigned int page_nb, int offset, int type)
{
    int channel, reg;
    int delay_ret;

    TIME_MICROSEC(_start);

    /* Calculate ch & reg */
    channel = flash_nb % CHANNEL_NB;
    ssd.cur_channel_mode[channel] = READ;
    reg = flash_nb*PLANES_PER_FLASH + block_nb%PLANES_PER_FLASH;

    /* Delay Operation */
    SSD_CH_ENABLE(flash_nb, channel);    // channel enable

    /* Access Register */
    if( IO_PARALLELISM == 0 ){
        delay_ret = SSD_FLASH_ACCESS(flash_nb, channel, reg);
    }
    else{
        delay_ret = SSD_REG_ACCESS(flash_nb, channel, reg);
    }

    /* Record Time Stamp */
    SSD_CH_RECORD(channel, offset, delay_ret);
    SSD_CELL_RECORD(reg, channel);
    SSD_REG_RECORD(reg, type, offset, channel);
    SSD_REG_ACCESS(flash_nb, channel, reg);

    TIME_MICROSEC(_end);

        if (ssd.prev_channel_mode[channel] != READ && ssd.prev_channel_mode[channel] != NOOP) {
            LOG_CHANNEL_SWITCH_TO_READ(GET_LOGGER(flash_nb), (ChannelSwitchToReadLog) {
                .channel = channel,
                .metadata = {_start, _end}
            });
        }
        ssd.prev_channel_mode[channel] = READ;

    LOG_PHYSICAL_CELL_READ(GET_LOGGER(flash_nb), (PhysicalCellReadLog) {
        .channel = channel, .block = block_nb, .page = page_nb,
        .metadata = {_start, _end}
    });

    return FTL_SUCCESS;
}

ftl_ret_val SSD_BLOCK_ERASE(unsigned int flash_nb, unsigned int block_nb)
{
    int channel, reg;

    TIME_MICROSEC(_start);

    /* Calculate ch & reg */
    channel = flash_nb % CHANNEL_NB;
    ssd.cur_channel_mode[channel] = ERASE;
    reg = flash_nb*PLANES_PER_FLASH + block_nb%PLANES_PER_FLASH;

    /* Delay Operation */
    if( IO_PARALLELISM == 0 ){
        SSD_FLASH_ACCESS(flash_nb, channel, reg);
    }
    else{
        SSD_REG_ACCESS(flash_nb, channel, reg);
    }

    /* Record Time Stamp */
    SSD_REG_RECORD(reg, ERASE, -1, channel);
    SSD_CELL_RECORD(reg, channel);

    TIME_MICROSEC(_end);

    ssd.occupied_pages_counter -= PAGE_NB;
    ssd.prev_channel_mode[channel] = ERASE;

    LOG_BLOCK_ERASE(GET_LOGGER(flash_nb), (BlockEraseLog) {
        .channel = channel, .die = flash_nb, .block = block_nb,
        .metadata = {_start, _end}
    });

    return FTL_SUCCESS;
}

int SSD_FLASH_ACCESS(unsigned int flash_nb, unsigned int channel, unsigned int reg)
{
    uint32_t i;
    uint32_t r_num = flash_nb * PLANES_PER_FLASH;
    int ret = 0;

    for (i=0;i<PLANES_PER_FLASH;i++) {

        if (r_num != reg && access_nb[r_num][0] == io_request_seq_nb) {
            /* That's OK */
        }
        else{
            ret = SSD_REG_ACCESS(flash_nb, channel, r_num);
        }

        r_num++;
    }

    return ret;
}

int SSD_REG_ACCESS(unsigned int flash_nb, int channel, int reg)
{
    switch (reg_io_cmd[reg]){
        case READ:
            return SSD_REG_READ_DELAY(flash_nb, channel, reg) + SSD_CELL_READ_DELAY(reg);
        case WRITE:
            return SSD_REG_WRITE_DELAY(flash_nb, channel, reg) + SSD_CELL_WRITE_DELAY(reg);
        case ERASE:
            return SSD_BLOCK_ERASE_DELAY(reg);
        case COPYBACK:
            return SSD_CELL_READ_DELAY(reg) + SSD_CELL_WRITE_DELAY(reg);
        case NOOP:
            return 0;
        default:
            PERR("SSD_REG_ACCESS: Command Error! %d\n", reg_io_cmd[reg]);
            return 0;
    }
}

int SSD_CH_ENABLE(unsigned int flash_nb, int channel)
{
    if(CHANNEL_SWITCH_DELAY_R == 0 && CHANNEL_SWITCH_DELAY_W == 0)
        return FTL_SUCCESS;

        //todo: currently writing on all channels at the same time takes more time than writing on one
    if(old_channel_nb != channel){
        SSD_CH_SWITCH_DELAY(flash_nb, channel);
    }

    return FTL_SUCCESS;
}

ftl_ret_val SSD_REG_RECORD(int reg, int type, int offset, int channel)
{
    reg_io_cmd[reg] = ssd.cur_channel_mode[channel];
    reg_io_type[reg] = type;
    ftl_ret_val retval = FTL_SUCCESS;

    switch (reg_io_cmd[reg]) {
        case WRITE:
            reg_io_time[reg] = old_channel_time;
            if (ssd.prev_channel_mode[channel] == READ) {
                reg_io_time[reg] += CHANNEL_SWITCH_DELAY_W;
            }
            // SSD_UPDATE_CH_ACCESS_TIME(channel, reg_io_time[reg]);

            /* Update SATA request Info */
            if(type == WRITE || type == SEQ_WRITE || type == RAN_WRITE || type == RAN_COLD_WRITE || type == RAN_HOT_WRITE){
                access_nb[reg][0] = io_request_seq_nb;
                access_nb[reg][1] = offset;
                io_update_overhead = UPDATE_IO_REQUEST(io_request_seq_nb, offset, old_channel_time, UPDATE_START_TIME);
                SSD_UPDATE_IO_OVERHEAD(reg, io_update_overhead);
            }
            else{
                access_nb[reg][0] = UINT32_MAX;
                access_nb[reg][1] = UINT32_MAX;
            }
        break;
    case READ:
        reg_io_time[reg] = SSD_GET_CH_ACCESS_TIME_FOR_READ(channel, reg);
        if (ssd.prev_channel_mode[channel] != READ && ssd.prev_channel_mode[channel] != NOOP) {
                reg_io_time[reg] += CHANNEL_SWITCH_DELAY_R;
            }
            /* Update SATA request Info */
            if(type == READ){
                access_nb[reg][0] = io_request_seq_nb;
                access_nb[reg][1] = offset;
                io_update_overhead = UPDATE_IO_REQUEST(io_request_seq_nb, offset, old_channel_time, UPDATE_START_TIME);
                SSD_UPDATE_IO_OVERHEAD(reg, io_update_overhead);
            }
            else{
                access_nb[reg][0] = UINT32_MAX;
                access_nb[reg][1] = UINT32_MAX;
            }
        break;
    case ERASE:
        /* Update SATA request Info */
        access_nb[reg][0] = UINT32_MAX;
        access_nb[reg][1] = UINT32_MAX;
        break;
    case COPYBACK:
        reg_io_time[reg] = cell_io_time[reg] + CELL_READ_DELAY;
        access_nb[reg][0] = io_request_seq_nb;
        access_nb[reg][1] = offset;
        io_update_overhead = UPDATE_IO_REQUEST(io_request_seq_nb, offset, old_channel_time, UPDATE_START_TIME);
        SSD_UPDATE_IO_OVERHEAD(reg, io_update_overhead);
        break;
    default:
        PERR("SSD_REG_RECORD: Command Error! %d\n", reg_io_cmd[reg]);
        retval = FTL_FAILURE;
        break;
    }
    return retval;
}

int SSD_CH_ACCESS(unsigned int flash_nb, int channel)
{
    uint32_t i, j;
    int ret = FTL_SUCCESS;
    uint32_t r_num;

    for (i=0;i<WAY_NB;i++) {
        r_num = channel*PLANES_PER_FLASH + i*CHANNEL_NB*PLANES_PER_FLASH;
        for(j=0;j<PLANES_PER_FLASH;j++){
            if(reg_io_time[r_num] <= get_usec() && reg_io_time[r_num] != -1){
                if(reg_io_cmd[r_num] == READ){
                    SSD_CELL_READ_DELAY(r_num);
                    SSD_REG_READ_DELAY(flash_nb, channel, r_num);
                    ret = FTL_FAILURE;
                }
                else if(reg_io_cmd[r_num] == WRITE){
                    SSD_REG_WRITE_DELAY(flash_nb, channel, r_num);
                    ret = FTL_FAILURE;
                }
            }
            r_num++;
        }
    }

    return ret;
}

void SSD_UPDATE_IO_OVERHEAD(int reg, int64_t overhead_time)
{
    io_overhead[reg] += overhead_time;
    io_alloc_overhead = 0;
    io_update_overhead = 0;
}

int64_t SSD_CH_SWITCH_DELAY(unsigned int flash_nb, int channel)
{
    int64_t start = 0;
    int64_t    end = 0;
    int64_t diff = 0;
    int64_t switch_delay = 0;

    TIME_MICROSEC(_start);
    if (ssd.cur_channel_mode[channel] == READ ) {
        switch_delay = CHANNEL_SWITCH_DELAY_R;
    }
    else if (ssd.cur_channel_mode[channel] == WRITE) {
        switch_delay = CHANNEL_SWITCH_DELAY_W;
    }
    else{
        return 0;
    }

    start = get_usec();
    diff = start - old_channel_time;

#ifdef DEL_QEMU_OVERHEAD
    if(diff < switch_delay){
        SSD_UPDATE_QEMU_OVERHEAD(switch_delay-diff);
    }
#endif

    if (diff < switch_delay){
        while( diff < switch_delay ){
            diff = get_usec() - old_channel_time;
        }
    }
    end = get_usec();

    TIME_MICROSEC(_end);

    
    if (ssd.cur_channel_mode[channel] == READ){
        LOG_CHANNEL_SWITCH_TO_READ(GET_LOGGER(flash_nb), (ChannelSwitchToReadLog) {
            .channel = channel,
            .metadata = {_start, _end}
        });
    }
    else if (ssd.cur_channel_mode[channel] == WRITE){
        LOG_CHANNEL_SWITCH_TO_WRITE(GET_LOGGER(flash_nb), (ChannelSwitchToWriteLog) {
            .channel = channel,
            .metadata = {_start, _end}
        });
    }
    return end-start;
}

int SSD_REG_WRITE_DELAY(unsigned int flash_nb, int channel, int reg)
{
    int ret = 0;
    int64_t start = 0;
    int64_t    end = 0;
    int64_t diff = 0;
    int64_t time_stamp = reg_io_time[reg];

    TIME_MICROSEC(_start);

    if (time_stamp == -1)
        return 0;

    /* Reg Write Delay */
    start = get_usec();
    diff = start - time_stamp;

#ifdef DEL_QEMU_OVERHEAD
    if(diff < REG_WRITE_DELAY){
        SSD_UPDATE_QEMU_OVERHEAD(REG_WRITE_DELAY-diff);
    }
    diff = start - reg_io_time[reg];
#endif

    int64_t delay = 0;
    if (diff < REG_WRITE_DELAY){
        delay = REG_WRITE_DELAY - diff;
        while( diff < REG_WRITE_DELAY ){
            diff = get_usec() - time_stamp;
        }
        ret = 1;
    }
    end = get_usec();
    (void) end; // Not Used Variable

    /* Send Delay Info To Perf Checker */
    SEND_TO_PERF_CHECKER(reg_io_type[reg], delay, CH_OP);

    /* Update Time Stamp Struct */
    reg_io_time[reg] = -1;
    reg_io_cmd[reg] = NOOP;

    TIME_MICROSEC(_end);

    LOG_REGISTER_WRITE(GET_LOGGER(flash_nb), (RegisterWriteLog) {
        .channel = channel, .die = flash_nb, .reg = reg,
        .metadata = {_start, _end}
    });

    return ret;
}

int SSD_REG_READ_DELAY(unsigned int flash_nb, int channel, int reg)
{
    int ret = 0;
    int64_t start = 0;
    int64_t end = 0;
    int64_t diff = 0;
    int64_t time_stamp = reg_io_time[reg];
    TIME_MICROSEC(_start);
    if (time_stamp == -1)
        return 0;

    /* Reg Read Delay */
    start = get_usec();
    diff = start - time_stamp;

#ifdef DEL_QEMU_OVERHEAD
    if(diff < REG_READ_DELAY){
        SSD_UPDATE_QEMU_OVERHEAD(REG_READ_DELAY-diff);
    }
    diff = start - reg_io_time[reg];
#endif

    if(diff < REG_READ_DELAY){
        while(diff < REG_READ_DELAY){
            diff = get_usec() - time_stamp;
        }
        ret = 1;
    }
    end = get_usec();

    /* Send Delay Info To Perf Checker */
    SEND_TO_PERF_CHECKER(reg_io_type[reg], end-start, CH_OP);
    SSD_UPDATE_IO_REQUEST(reg);

    /* Update Time Stamp Struct */
    reg_io_time[reg] = -1;
    reg_io_cmd[reg] = NOOP;

    TIME_MICROSEC(_end);

    LOG_REGISTER_READ(GET_LOGGER(flash_nb), (RegisterReadLog) {
        .channel = channel, .die = flash_nb, .reg = reg,
        .metadata = {_start, _end}
    });

    return ret;
}

int SSD_CELL_WRITE_DELAY(int reg)
{
    int ret = 0;
    int64_t start = 0;
    int64_t end = 0;
    int64_t diff = 0;
    int64_t time_stamp = cell_io_time[reg];

    if( time_stamp == -1 )
        return 0;

    /* Cell Write Delay */
    start = get_usec();
    diff = start - time_stamp + io_overhead[reg];

#ifdef DEL_QEMU_OVERHEAD
    if(diff < CELL_PROGRAM_DELAY){
        SSD_UPDATE_QEMU_OVERHEAD(CELL_PROGRAM_DELAY-diff);
    }
    diff = start - cell_io_time[reg] + io_overhead[reg];
#endif

    if( diff < CELL_PROGRAM_DELAY){
        init_diff_reg = diff;
        while(diff < CELL_PROGRAM_DELAY){
            diff = get_usec() - time_stamp + io_overhead[reg];
        }
        ret = 1;
    }
    end = get_usec();
    (void) end; // Not Used Variable

    /* Send Delay Info To Perf Checker */
    SEND_TO_PERF_CHECKER(reg_io_type[reg], diff, REG_OP);
    SSD_UPDATE_IO_REQUEST(reg);

    /* Update Time Stamp Struct */
    cell_io_time[reg] = -1;
    reg_io_type[reg] = NOOP;

    /* Update IO Overhead */
    io_overhead[reg] = 0;

    return ret;
}

int SSD_CELL_READ_DELAY(int reg)
{
    int ret = 0;
    int64_t start = 0;
    int64_t end = 0;
    int64_t diff = 0;
    int64_t time_stamp = cell_io_time[reg];

    int64_t REG_DELAY = CELL_READ_DELAY;

    if( time_stamp == -1)
        return 0;

    /* Cell Read Delay */
    start = get_usec();
    diff = start - time_stamp + io_overhead[reg];

#ifdef DEL_QEMU_OVERHEAD
    if( diff < REG_DELAY){
        SSD_UPDATE_QEMU_OVERHEAD(REG_DELAY-diff);
    }
    diff = start - cell_io_time[reg] + io_overhead[reg];
#endif

    if( diff < REG_DELAY){
        init_diff_reg = diff;
        while( diff < REG_DELAY ){
            diff = get_usec() - time_stamp + io_overhead[reg];
        }
        ret = 1;

    }
    end = get_usec();

    /* Send Delay Info To Perf Checker */
    SEND_TO_PERF_CHECKER(reg_io_type[reg], end-start, REG_OP);

    /* Update Time Stamp Struct */
    cell_io_time[reg] = -1;
    reg_io_type[reg] = NOOP;

    /* Update IO Overhead */
    io_overhead[reg] = 0;

    return ret;
}

int SSD_BLOCK_ERASE_DELAY(int reg)
{
    int ret = 0;
    int64_t start = 0;
    int64_t end = 0;
    int64_t diff;
    int64_t time_stamp = cell_io_time[reg];

    if( time_stamp == -1)
        return 0;

    /* Block Erase Delay */
    start = get_usec();
    diff = get_usec() - cell_io_time[reg];
    if( diff < BLOCK_ERASE_DELAY){
        while(diff < BLOCK_ERASE_DELAY){
            diff = get_usec() - time_stamp;
        }
        ret = 1;
    }
    end = get_usec();
    (void) start; // Not Used Variable
    (void) end; // Not Used Variable

    /* Update IO Overhead */
    cell_io_time[reg] = -1;
    reg_io_cmd[reg] = NOOP;
    reg_io_type[reg] = NOOP;

    return ret;
}

int64_t SSD_GET_CH_ACCESS_TIME_FOR_READ(int channel, int reg)
{
    uint32_t i, j;
    uint32_t r_num;
    int64_t latest_time = cell_io_time[reg] + CELL_READ_DELAY;

    int64_t temp_time = 0;

    for(i=0;i<WAY_NB;i++){
        r_num = channel*PLANES_PER_FLASH + i*CHANNEL_NB*PLANES_PER_FLASH;
        for(j=0;j<PLANES_PER_FLASH;j++){
            temp_time = 0;

            if(reg_io_cmd[r_num] == READ){
                temp_time = reg_io_time[r_num] + REG_READ_DELAY;
            }
            else if(reg_io_cmd[r_num] == WRITE){
                temp_time = reg_io_time[r_num] + REG_WRITE_DELAY;
            }

            if( temp_time > latest_time ){
                latest_time = temp_time;
            }
            r_num++;
        }
    }

    return latest_time;
}

void SSD_UPDATE_CH_ACCESS_TIME(int channel, int64_t current_time)
{
    uint32_t i, j;
    uint32_t r_num;

    for(i=0;i<WAY_NB;i++){
        r_num = channel*PLANES_PER_FLASH + i*CHANNEL_NB*PLANES_PER_FLASH;
        for(j=0;j<PLANES_PER_FLASH;j++){
            if(reg_io_cmd[r_num] == READ && reg_io_time[r_num] > current_time ){
                reg_io_time[r_num] += REG_WRITE_DELAY;
            }
            r_num++;
        }
    }
}

void SSD_UPDATE_IO_REQUEST(int reg)
{
    int64_t curr_time = get_usec();
    if(init_diff_reg != 0){
        io_update_overhead = UPDATE_IO_REQUEST(access_nb[reg][0], access_nb[reg][1], curr_time, UPDATE_END_TIME);
        SSD_UPDATE_IO_OVERHEAD(reg, io_update_overhead);
        access_nb[reg][0] = UINT32_MAX;
    }
    else{
        io_update_overhead = UPDATE_IO_REQUEST(access_nb[reg][0], access_nb[reg][1], 0, UPDATE_END_TIME);
        SSD_UPDATE_IO_OVERHEAD(reg, io_update_overhead);
        access_nb[reg][0] = UINT32_MAX;
    }
}

void SSD_REMAIN_IO_DELAY(unsigned int flash_nb, int channel, int reg)
{
    SSD_REG_ACCESS(flash_nb, channel, reg);
}

//MIX
int64_t qemu_overhead;

void SSD_UPDATE_QEMU_OVERHEAD(int64_t delay)
{
    int i;
    int p_num = FLASH_NB * PLANES_PER_FLASH;
    int64_t diff = delay;

    if(qemu_overhead == 0){
        return;
    }
    else{
        if(diff > qemu_overhead){
            diff = qemu_overhead;
        }
    }

    old_channel_time -= diff;
    for(i=0;i<p_num;i++){
        cell_io_time[i] -= diff;
        reg_io_time[i] -= diff;
    }
    qemu_overhead -= diff;
}

ftl_ret_val SSD_PAGE_COPYBACK(uint32_t source, uint32_t destination, int type){

    uint32_t flash_nb;
    uint32_t source_plane, destination_plane;
    uint32_t reg , channel , delay_ret;

    //Check source and destination pages are at the same plane.
    source_plane = CALC_FLASH(source)*PLANES_PER_FLASH + CALC_BLOCK(source)%PLANES_PER_FLASH;
    destination_plane = CALC_FLASH(destination)*PLANES_PER_FLASH + CALC_BLOCK(destination)%PLANES_PER_FLASH;
    if (source_plane != destination_plane){
        //copyback from different planes is not supported
        return FTL_FAILURE;
    }else{
        reg = destination_plane;
        flash_nb = CALC_FLASH(source);
    }

    channel = flash_nb % CHANNEL_NB;
    ssd.cur_channel_mode[channel] = COPYBACK;

    /* Delay Operation */
    //SSD_CH_ENABLE(flash_nb, channel);    // channel enable

    /* Access Register */
    if( IO_PARALLELISM == 0 ){
        delay_ret = SSD_FLASH_ACCESS(flash_nb, channel, reg);
    }
    else{
        delay_ret = SSD_REG_ACCESS(flash_nb, channel, reg);
    }

    SSD_CH_RECORD(channel, 0, delay_ret);
    SSD_CELL_RECORD(reg, channel);
    SSD_REG_RECORD(reg, type, 0, channel);

    return FTL_SUCCESS;
}

double SSD_UTIL(void) {
    return (double)ssd.occupied_pages_counter / PAGES_IN_SSD;
}
