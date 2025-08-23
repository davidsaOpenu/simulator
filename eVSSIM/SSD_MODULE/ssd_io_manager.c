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

unsigned int old_channel_nb;
int64_t last_operation_time_us;

int64_t init_diff_reg=0;

int64_t io_alloc_overhead=0;
int64_t io_update_overhead=0;

char ssd_version[4] = "1.0";
char ssd_date[9] = "13.04.11";

struct timeval logging_parser_tv;

ssd_disk ssd;

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
        return start_wall_time_us + sim_time_us + time_delay;
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
        sim_time_us += usec;
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

int SSD_IO_INIT(void){

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
    old_channel_nb = CHANNEL_NB;
    last_operation_time_us = start_wall_time_us;

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

    SSDTimeMode = EMULATED;

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
            cell_io_time[reg] = last_operation_time_us + REG_WRITE_DELAY;
            break;
        case READ:
            cell_io_time[reg] = last_operation_time_us;
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

static int SSD_CH_RECORD(void)
{
    int64_t now = get_usec();
    if (now < last_operation_time_us) now = last_operation_time_us;
    last_operation_time_us = now;
    return FTL_SUCCESS;
}

ftl_ret_val SSD_PAGE_WRITE(unsigned int flash_nb, unsigned int block_nb, unsigned int page_nb, int offset, int type)
{
    uint32_t channel, reg;
    int ret = FTL_FAILURE;

    int64_t _start = get_usec();

    /* Calculate ch & reg */
    channel = flash_nb % CHANNEL_NB;
    ssd.cur_channel_mode[channel] = WRITE;
    reg = flash_nb*PLANES_PER_FLASH + block_nb%PLANES_PER_FLASH;

    /* Delay Operation */
    SSD_CH_ENABLE(flash_nb, channel);    // channel enable

    if( IO_PARALLELISM == 0 ){
        SSD_FLASH_ACCESS(flash_nb, channel, reg);
    }
    else{
        SSD_REG_ACCESS(flash_nb, channel, reg);
    }

    /* Check Channel Operation */
    while(ret == FTL_FAILURE){
        ret = SSD_CH_ACCESS(flash_nb, channel);
    }

    /* Record Time Stamp */
    SSD_CH_RECORD();
    SSD_CELL_RECORD(reg, channel);
    SSD_REG_RECORD(reg, type, offset, channel);
    SSD_REG_ACCESS(flash_nb, channel, reg);

    int64_t _end = get_usec();

    if (old_channel_nb == channel && ssd.prev_channel_mode[channel] != WRITE) {
    LOG_CHANNEL_SWITCH_TO_WRITE(GET_LOGGER(flash_nb), (ChannelSwitchToWriteLog) {
    .channel = channel, .metadata = {
      _start,
      _end
    }
    });
    }

    ssd.prev_channel_mode[channel] = WRITE;
    old_channel_nb = channel;

    /* Update ssd page write counters */
    if (type != WRITE_COMMIT) {
        ssd.occupied_pages_counter++;
        SSD_UTIL_LOG(flash_nb);
    }
    ssd.physical_page_writes++;

    inverse_block_mapping_entry* block_entry = GET_INVERSE_BLOCK_MAPPING_ENTRY(flash_nb, block_nb);
    block_entry->dirty_page_nb++;

    if (type == WRITE_COMMIT) {
        LOG_PHYSICAL_CELL_PROGRAM_COMPATIBLE(GET_LOGGER(flash_nb), (PhysicalCellProgramCompatibleLog) {
            .channel = channel, .block = block_nb, .page = page_nb,
            .metadata = {_start, _end}
        });
    }
    else {
        LOG_PHYSICAL_CELL_PROGRAM(GET_LOGGER(flash_nb), (PhysicalCellProgramLog) {
            .channel = channel, .block = block_nb, .page = page_nb,
            .metadata = {_start, _end}
        });
    }


    if (type == WRITE || type == WRITE_COMMIT) { // if we log logical write first, write amp may get negative
        ssd.logical_page_writes++;

        LOG_LOGICAL_CELL_PROGRAM(GET_LOGGER(flash_nb),(LogicalCellProgramLog) {
            .channel = channel, .block = block_nb, .page = page_nb,
            .metadata = {_start, _end}
        });
    }

    return ret;
}

ftl_ret_val SSD_PAGE_READ(unsigned int flash_nb, unsigned int block_nb, unsigned int page_nb, int offset, int type)
{
    unsigned int channel, reg;

    int64_t _start = get_usec();

    /* Calculate ch & reg */
    channel = flash_nb % CHANNEL_NB;
    ssd.cur_channel_mode[channel] = READ;
    reg = flash_nb*PLANES_PER_FLASH + block_nb%PLANES_PER_FLASH;

    /* Delay Operation */
    SSD_CH_ENABLE(flash_nb, channel);    // channel enable

    /* Access Register */
    if( IO_PARALLELISM == 0 ){
        SSD_FLASH_ACCESS(flash_nb, channel, reg);
    }
    else{
        SSD_REG_ACCESS(flash_nb, channel, reg);
    }

    /* Record Time Stamp */
    SSD_CH_RECORD();
    SSD_CELL_RECORD(reg, channel);
    SSD_REG_RECORD(reg, type, offset, channel);
    SSD_REG_ACCESS(flash_nb, channel, reg);

    int64_t _end = get_usec();

    if (old_channel_nb == channel && ssd.prev_channel_mode[channel] != READ && ssd.prev_channel_mode[channel] != NOOP) {
    LOG_CHANNEL_SWITCH_TO_READ(GET_LOGGER(flash_nb), (ChannelSwitchToReadLog) {
    .channel = channel, .metadata = {
      _start,
      _end
    }
    });
    }

    ssd.prev_channel_mode[channel] = READ;
    old_channel_nb = channel;

    LOG_PHYSICAL_CELL_READ(GET_LOGGER(flash_nb), (PhysicalCellReadLog) {
        .channel = channel, .block = block_nb, .page = page_nb,
        .metadata = {_start, _end}
    });

    return FTL_SUCCESS;
}

ftl_ret_val SSD_BLOCK_ERASE(unsigned int flash_nb, unsigned int block_nb)
{
    int channel, reg;

    int64_t _start = get_usec();

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

    int64_t _end = get_usec();

    inverse_block_mapping_entry* block_entry = GET_INVERSE_BLOCK_MAPPING_ENTRY(flash_nb, block_nb);

    ssd.occupied_pages_counter -= block_entry->dirty_page_nb;
    SSD_UTIL_LOG(flash_nb);
    ssd.prev_channel_mode[channel] = ERASE;

    LOG_BLOCK_ERASE(GET_LOGGER(flash_nb), (BlockEraseLog) {
        .channel = channel, .die = flash_nb, .block = block_nb, .dirty_page_nb = block_entry->dirty_page_nb,
        .metadata = {_start, _end}
    });

    block_entry->dirty_page_nb = 0;

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

int SSD_CH_ENABLE(unsigned int flash_nb, unsigned int channel)
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
            reg_io_time[reg] = last_operation_time_us;
            if (ssd.prev_channel_mode[channel] == READ) {
                reg_io_time[reg] += CHANNEL_SWITCH_DELAY_W;
            }
            // SSD_UPDATE_CH_ACCESS_TIME(channel, reg_io_time[reg]);

            /* Update SATA request Info */
            if(type == WRITE) {
                access_nb[reg][0] = io_request_seq_nb;
                access_nb[reg][1] = offset;
                io_update_overhead = UPDATE_IO_REQUEST(io_request_seq_nb, offset, last_operation_time_us, UPDATE_START_TIME);
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
                io_update_overhead = UPDATE_IO_REQUEST(io_request_seq_nb, offset, last_operation_time_us, UPDATE_START_TIME);
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
        io_update_overhead = UPDATE_IO_REQUEST(io_request_seq_nb, offset, last_operation_time_us, UPDATE_START_TIME);
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
    const int mode = ssd.cur_channel_mode[channel];

    /* Determine configured switch delay (only READ/WRITE have one) */
    int64_t switch_delay =
        (mode == READ) ? CHANNEL_SWITCH_DELAY_R : (mode == WRITE) ? CHANNEL_SWITCH_DELAY_W
                                                                  : 0;

    int64_t _start = get_usec();

    /* Absolute scheduling against last channel activity time.*/
    if (switch_delay > 0)
    {
        const int64_t target = last_operation_time_us + switch_delay;
        wait_until(target); /* advances emulated time deterministically (no-op if already >= target) */
    }

    int64_t _end = get_usec();

    /* Emit channel-switch logs */
    switch (mode)
    {
    case READ:
        LOG_CHANNEL_SWITCH_TO_READ(GET_LOGGER(flash_nb), (ChannelSwitchToReadLog){
                                                             .channel = channel,
                                                             .metadata = {_start, _end}});
        break;

    case WRITE:
        LOG_CHANNEL_SWITCH_TO_WRITE(GET_LOGGER(flash_nb), (ChannelSwitchToWriteLog){
                                                              .channel = channel,
                                                              .metadata = {_start, _end}});
        break;

    case COPYBACK:
        /* TODO: log channel switch to copyback (no specific delay defined) */
        break;

    default:
    {
        const int64_t dur = _end - _start;
        RERR(dur, "New Channel mode unexpected : %d", mode);
        break;
    }
    }

    return _end - _start;
}


int SSD_REG_WRITE_DELAY(unsigned int flash_nb, int channel, int reg)
{
    int ret = 0;

    /* Absolute scheduled start for this register op */
    const int64_t sched_start = reg_io_time[reg];
    if (sched_start == -1)
        return 0;

    int64_t _start = get_usec();

    const int64_t now = get_usec();
    const int64_t target = sched_start + REG_WRITE_DELAY;
    const int64_t delay = (target > now) ? (target - now) : 0;

    if (delay > 0)
    {
        wait_until(target); /* advances emulated time deterministically */
        ret = 1;            /* we actually waited */
    }

    /* Channel-level perf accounting: how long this register write waited */
    SEND_TO_PERF_CHECKER(reg_io_type[reg], delay, CH_OP);

    /* Clear schedule markers for this register slot */
    reg_io_time[reg] = -1;
    reg_io_cmd[reg] = NOOP;

    int64_t _end = get_usec();

    LOG_REGISTER_WRITE(GET_LOGGER(flash_nb), (RegisterWriteLog){
                                                 .channel = channel, .die = flash_nb, .reg = reg, .metadata = {_start, _end}});

    return ret;
}

int SSD_REG_READ_DELAY(unsigned int flash_nb, int channel, int reg)
{
    int ret = 0;

    /* Absolute scheduled start for this register op */
    const int64_t sched_start = reg_io_time[reg];
    if (sched_start == -1)
        return 0;

    int64_t _start = get_usec();
    const int64_t now = get_usec();
    const int64_t target = sched_start + REG_READ_DELAY;
    const int64_t delay = (target > now) ? (target - now) : 0;

    /* Reg Read Delay */
    if (delay > 0)
    {
        wait_until(target); /* advances emulated time deterministically */
        ret = 1;            /* we actually waited */
    }

    /* Channel-level perf accounting + IO request bookkeeping */
    SEND_TO_PERF_CHECKER(reg_io_type[reg], delay, CH_OP);
    SSD_UPDATE_IO_REQUEST(reg);

    /* Update Time Stamp Struct */
    reg_io_time[reg] = -1;
    reg_io_cmd[reg] = NOOP;

    int64_t _end = get_usec();

    LOG_REGISTER_READ(GET_LOGGER(flash_nb), (RegisterReadLog){
                                                .channel = channel, .die = flash_nb, .reg = reg, .metadata = {_start, _end}});

    return ret;
}

int SSD_CELL_WRITE_DELAY(int reg)
{
    int ret = 0;

    /* Absolute scheduled start for this cell program op */
    const int64_t sched_start = cell_io_time[reg];
    if (sched_start == -1)
        return 0;

    int64_t _start = get_usec();

    /* Target completion = scheduled start + accumulated overhead + program delay */
    const int64_t now = get_usec();
    const int64_t target = sched_start + io_overhead[reg] + CELL_PROGRAM_DELAY;
    const int64_t wait_us = (target > now) ? (target - now) : 0;

    if (wait_us > 0)
    {
        /* Preserve legacy behavior: set init_diff_reg only when we actually wait */
        const int64_t elapsed = (now - sched_start) + io_overhead[reg];
        init_diff_reg = elapsed;

        wait_until(target); /* deterministically advance emulated time */
        ret = 1;
    }

    int64_t _end = get_usec();

    /* Channel/register perf accounting and IO bookkeeping */
    SEND_TO_PERF_CHECKER(reg_io_type[reg], (_end - _start), REG_OP);
    SSD_UPDATE_IO_REQUEST(reg);

    /* Clear schedule markers for this cell slot */
    cell_io_time[reg] = -1;
    reg_io_type[reg] = NOOP;

    /* Reset accumulated overhead for this register/cell */
    io_overhead[reg] = 0;

    return ret;
}


int SSD_CELL_READ_DELAY(int reg)
{
    int ret = 0;

    /* Scheduled start for this cell read */
    const int64_t sched_start = cell_io_time[reg];
    if (sched_start == -1)
        return 0;

    int64_t _start = get_usec();

    /* Completion target = scheduled start + overhead + configured read delay */
    const int64_t now = get_usec();
    const int64_t target = sched_start + io_overhead[reg] + CELL_READ_DELAY;
    const int64_t wait_us = (target > now) ? (target - now) : 0;

    if (wait_us > 0)
    {
        /* Preserve legacy behavior: set init_diff_reg only when we actually wait */
        const int64_t elapsed = (now - sched_start) + io_overhead[reg];
        init_diff_reg = elapsed;

        wait_until(target); /* deterministically advance emulated time */
        ret = 1;
    }

    int64_t _end = get_usec();

    /* Channel/register perf accounting and IO bookkeeping */
    SEND_TO_PERF_CHECKER(reg_io_type[reg], (_end - _start), REG_OP);
    SSD_UPDATE_IO_REQUEST(reg);

    /* Clear schedule markers for this cell slot */
    cell_io_time[reg] = -1;
    reg_io_type[reg] = NOOP;

    /* Reset accumulated overhead for this register/cell */
    io_overhead[reg] = 0;

    return ret;
}

int SSD_BLOCK_ERASE_DELAY(int reg)
{
    int ret = 0;
    const int64_t sched_start = cell_io_time[reg];

    if (sched_start == -1)
        return 0;

    /* Completion target = scheduled start + configured erase delay */
    const int64_t now = get_usec();
    const int64_t target = sched_start + BLOCK_ERASE_DELAY;

    if (target > now)
    {
        wait_until(target); /* deterministically advance emulated time */
        ret = 1;
    }

    /* Clear scheduling markers */
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

    last_operation_time_us -= diff;
    for(i=0;i<p_num;i++){
        cell_io_time[i] -= diff;
        reg_io_time[i] -= diff;
    }
    qemu_overhead -= diff;
}

ftl_ret_val SSD_PAGE_COPYBACK(uint32_t source, uint32_t destination, int type){

    uint32_t flash_nb, block_nb;
    uint32_t dest_flash_nb, dest_block_nb;
    uint32_t source_plane, destination_plane;
    uint32_t reg , channel;

    //Check source and destination pages are at the same plane.
    block_nb = CALC_BLOCK(source);
    source_plane = CALC_FLASH(source)*PLANES_PER_FLASH + block_nb%PLANES_PER_FLASH;
    destination_plane = CALC_FLASH(destination)*PLANES_PER_FLASH + block_nb%PLANES_PER_FLASH;
    if (source_plane != destination_plane){
        //copyback from different planes is not supported
        return FTL_FAILURE;
    }else{
        reg = destination_plane;
        flash_nb = CALC_FLASH(source);
    }

    channel = flash_nb % CHANNEL_NB;
    ssd.cur_channel_mode[channel] = COPYBACK;

    int64_t _start = get_usec();

    /* Delay Operation */
    //SSD_CH_ENABLE(flash_nb, channel);    // channel enable

    /* Access Register */
    if( IO_PARALLELISM == 0 ){
        SSD_FLASH_ACCESS(flash_nb, channel, reg);
    }
    else{
        SSD_REG_ACCESS(flash_nb, channel, reg);
    }

    SSD_CH_RECORD();
    SSD_CELL_RECORD(reg, channel);
    SSD_REG_RECORD(reg, type, 0, channel);

    ssd.occupied_pages_counter++;
    SSD_UTIL_LOG(flash_nb);
    ssd.physical_page_writes++;

    dest_block_nb = CALC_BLOCK(destination);
    dest_flash_nb = CALC_FLASH(destination);
    inverse_block_mapping_entry* block_entry = GET_INVERSE_BLOCK_MAPPING_ENTRY(dest_flash_nb, dest_block_nb);
    block_entry->dirty_page_nb++;

    ssd.prev_channel_mode[channel] = COPYBACK;

    int64_t _end = get_usec();

    LOG_PAGE_COPYBACK(GET_LOGGER(flash_nb), (PageCopyBackLog) {
        .channel = channel, .block = block_nb, .source_page = source, .destination_page = destination,
        .metadata = {_start, _end}
    });


    return FTL_SUCCESS;
}

double SSD_UTIL(void) {
    const uint64_t total_pages    = (uint64_t)PAGES_IN_SSD;
    const uint64_t occupied_pages = (uint64_t)ssd.occupied_pages_counter;
    if (total_pages == 0) return 0.0;
    return (double)occupied_pages / (double)total_pages;
}

void SSD_UTIL_LOG(unsigned  flash_nb) {
    int64_t now = get_usec();

    const uint64_t total_pages    = (uint64_t)PAGES_IN_SSD;
    const uint64_t occupied_pages = (uint64_t)ssd.occupied_pages_counter;
    const double utilization = SSD_UTIL();

    // TODO: We are currently logging to flash_nb but the log is system wide
    // We might want to add a GET_SYSTEM_LOGGER() or a system_logger
    LOG_SSD_UTILIZATION(GET_LOGGER(flash_nb), (SsdUtilizationLog){
        .utilization_percent = utilization,
        .total_pages         = total_pages,
        .occupied_pages      = occupied_pages,
        .metadata            = {now, now}
    });
}