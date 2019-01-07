// Copyright(c)2013
//
// Hanyang University, Seoul, Korea
// Embedded Software Systems Lab. All right reserved

#include "common.h"

int64_t io_alloc_overhead=0;

char ssd_version[4] = "1.0";
char ssd_date[9] = "13.04.11";

struct timeval logging_parser_tv;

/**
 * Operations executions counter
 */
ssd_disk ssd;

//
// get curret timestamp by usec
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

ftl_ret_val SSD_PAGE_WRITE(unsigned int flash_nb, unsigned int block_nb, unsigned int page_nb, int offset, int type, int io_page_nb)
{
	int channel = flash_nb % CHANNEL_NB;

	/**
	 * Operation validation
	 */
	// validate page exists
	if( flash_nb >= FLASH_NB
			|| block_nb >= BLOCK_NB
			|| page_nb >= PAGE_NB )
		return FTL_FAILURE;

	// TODO un-comment on fixing object_tests
//	// validate page is free
//	if( page_map[flash_nb][block_nb][page_nb] == 1 )
//		return FTL_FAILURE;

    __atomic_fetch_add(&ssd.channels[channel].op_nb, 1, __ATOMIC_SEQ_CST);
	int current_op_nb = ssd.channels[channel].op_nb;

	/**
	 * Delay execution & measurement
	 */
	// measure start time
	int64_t start_usec = get_usec();
    TIME_MICROSEC(_start);
    // register write
    _SSD_REGISTER_WRITE(current_op_nb, flash_nb);
	// wait for register write done
    _SSD_CHANNEL_ACCESS_REAL(channel);
	// cell programming
	_SSD_SET_BLOCK_CELL_PROGRAMMING(flash_nb, block_nb);
	// wait for cell programming done
	_SSD_IO_WAIT(flash_nb, block_nb);
    __atomic_fetch_add(&(ssd.channels[channel].done_op_nb), 1, __ATOMIC_SEQ_CST);
    int done_op_nb = ssd.channels[channel].done_op_nb;
    ssd.channels[channel].done_op_nb = 0;
    ssd.channels[channel].done_op_nb = done_op_nb;
	// end of delay time
    TIME_MICROSEC(_end);
	int64_t done_usec = get_usec();
	int plane = CALC_PLANE_BY_FLASH_BLOCK(flash_nb, block_nb);

	unsigned int delay = (get_usec() - start_usec);

	/**
	 * Statistics update
	 */
	ssd.write_delay += delay;
	// page (physical) writes counter increase
	ssd.physical_page_writes++;
    // page occupy, update occupied pages counter
    ssd.occupied_pages++;
    // number of in use pages on block @block_nb
    // on flash @flash_nb counter
    (ssd.blocks[flash_nb][block_nb].occupied_pages_counter)++;
    // on logical write increase logical
    // page write counter
    if( type == WRITE )
    	ssd.logical_page_writes++;
    //
    // mark page as occupied
//    page_map[flash_nb][block_nb][page_nb] = 1;

    /**
     * Logging monitor
     */
    //
    // send logs to monitor (old):
    // write page, write amplification,
    // write bandwidth, SSD util
	WRITE_LOG("WRITE PAGE %d\n", 1);
    WRITE_LOG("WB AMP %lf\n", (double)ssd.physical_page_writes / ssd.logical_page_writes);
    WRITE_LOG("WRITE BW %lf\n", SSD_WRITE_BANDWIDTH());
    WRITE_LOG("UTIL %lf\n", SSD_UTIL_CALC());
    // send physical cell log
    // to new monitor
    LOG_PHYSICAL_CELL_PROGRAM(GET_LOGGER(flash_nb), (PhysicalCellProgramLog) {
	    .channel = channel, .block = block_nb, .page = page_nb,
        .metadata.logging_start_time = _start,
        .metadata.logging_end_time = _end
	});

	return FTL_SUCCESS;
}

ftl_ret_val SSD_PAGE_READ(unsigned int flash_nb, unsigned int block_nb, unsigned int page_nb, int offset, int type, int io_page_nb)
{
	int channel = flash_nb % CHANNEL_NB;

	/**
	 * Operation validation
	 */
	// validate page exists
	if( flash_nb >= FLASH_NB
			|| block_nb >= BLOCK_NB
			|| page_nb >= PAGE_NB )
		return FTL_FAILURE;
	// TODO un-comment on fix object_tests
//	// validate page is written
//	if( page_map[flash_nb][block_nb][page_nb] == 0 )
//		return FTL_FAILURE;

	/**
	 * Delay execution & measurement
	 */
	// measure start delay time
	int64_t start_usec = get_usec();
    TIME_MICROSEC(_start);

    __atomic_fetch_add(&ssd.channels[channel].op_nb, 1, __ATOMIC_SEQ_CST);
	int current_op_nb = ssd.channels[channel].op_nb;

    _SSD_SET_REGISTER_READ(current_op_nb, flash_nb);
    _SSD_CHANNEL_ACCESS_REAL(channel);
    _SSD_SET_BLOCK_READ(flash_nb, block_nb);
    _SSD_IO_WAIT(flash_nb, block_nb);
    __atomic_fetch_add(&ssd.channels[channel].done_op_nb, 1, __ATOMIC_SEQ_CST);

    int duration = get_usec()-start_usec;
    // measure end delay time
    TIME_MICROSEC(_end);
	/**
	 * Statistics update
	 */
	ssd.physical_page_reads++;
    if( type == READ ) {
    	// count duration as read delay
    	// on logical page read
    	ssd.read_delay += duration;
    	ssd.logical_page_reads++;
    } else if( type == GC_READ ) {
    	// count duration as read delay
    	// on logical GC page read
    	ssd.write_delay += duration;
    }

    /**
     * Logging monitor
     */
    // send log to new monitor
	LOG_PHYSICAL_CELL_READ(GET_LOGGER(flash_nb), (PhysicalCellReadLog) {
	    .channel = channel, .block = block_nb, .page = page_nb,
        .metadata.logging_start_time = _start,
        .metadata.logging_end_time = _end
	});

	return FTL_SUCCESS;
}

ftl_ret_val SSD_BLOCK_ERASE(unsigned int flash_nb, unsigned int block_nb)
{
	int i;
	int channel = flash_nb % CHANNEL_NB;

	/**
	 * Operation validation
	 */
	// validate block exists
	if( flash_nb >= FLASH_NB
			|| block_nb >= BLOCK_NB )
		return FTL_FAILURE;

	/**
	 * Delay execution & measurement
	 */
	// measure start delay time
	int64_t start_usec = get_usec();
    TIME_MICROSEC(_start);

    __atomic_fetch_add(&ssd.channels[channel].op_nb, 1, __ATOMIC_SEQ_CST);
	int current_op_nb = ssd.channels[channel].op_nb;

    _SSD_CHANNEL_ACCESS(current_op_nb, channel);
    __atomic_fetch_add(&ssd.channels[channel].done_op_nb, 1, __ATOMIC_SEQ_CST);
    _SSD_SET_BLOCK_ERASE(flash_nb, block_nb);
    _SSD_IO_WAIT(flash_nb, block_nb);

    // measure end delay time
    TIME_MICROSEC(_end);
    // count erase operations delay as write delay
	unsigned int delay = (get_usec() - start_usec);

	/**
	 * Statistics update
	 */
	ssd.write_delay += delay;
    // occupied page in block released, update occupied pages counter
	ssd.occupied_pages -= ssd.blocks[flash_nb][block_nb].occupied_pages_counter;
	// calculate current occupied pages counter
	// and set number of pages as 0
    ssd.blocks[flash_nb][block_nb].occupied_pages_counter = 0;
    // set block's pages as free
//    for( i = 0; i < PAGE_NB; i++ ) {
//    	page_map[flash_nb][block_nb][i] = 0;
//    }
    // increase erase counter
    ssd.erases++;

    /**
     * Logging monitor
     */
    // notify monitor (old) by erase operation
    WRITE_LOG("ERASE ");
    WRITE_LOG("WRITE BW %lf\n", SSD_WRITE_BANDWIDTH());
    // notify new monitor by erase operation
	LOG_BLOCK_ERASE(GET_LOGGER(flash_nb), (BlockEraseLog) {
	    .channel = channel, .die = flash_nb, .block = block_nb,
        .metadata.logging_start_time = _start,
        .metadata.logging_end_time = _end
	});

	return FTL_SUCCESS;
}

ftl_ret_val SSD_PAGE_COPYBACK(uint32_t source, uint32_t destination, int type)
{

	int source_plane, destination_plane;
	int flash_nb, block_nb, channel;
	//Check source and destination pages are at the same plane.
	source_plane = CALC_PLANE(source);
	destination_plane = CALC_PLANE(destination);

	/**
	 * Operation validation
	 */
	if (source_plane != destination_plane)
		//copyback from different planes is not supported
		return FTL_FAILURE;

	flash_nb = CALC_FLASH(source);
	block_nb = CALC_BLOCK(source);

	// validate block exists
	if( flash_nb >= FLASH_NB
			|| block_nb >= BLOCK_NB)
		return FTL_FAILURE;

	channel = flash_nb % CHANNEL_NB;

    __atomic_fetch_add(&ssd.channels[channel].op_nb, 1, __ATOMIC_SEQ_CST);
	int current_op_nb = ssd.channels[channel].op_nb;


	/**
	 * Delay execution & measurement
	 */
	// measure start delay time
	int64_t start_usec = get_usec();

    _SSD_SET_REGISTER_READ(current_op_nb, flash_nb);
    _SSD_CHANNEL_ACCESS(current_op_nb, channel);
    _SSD_SET_BLOCK_PAGE_COPYBACK(flash_nb, block_nb);
    _SSD_IO_WAIT(flash_nb, block_nb);
    __atomic_fetch_add(&ssd.channels[channel].done_op_nb, 1, __ATOMIC_SEQ_CST);

    // count page copy back operation delay as write delay
	unsigned int delay = (get_usec() - start_usec);

	/**
	 * Statistics update
	 */
	ssd.write_delay += delay;

	return FTL_SUCCESS;
}

void _SSD_DISK_INIT(ssd_disk* ssd
		, unsigned int flash_nb
		, unsigned int block_nb
		, unsigned int channel_nb
		, unsigned int planes_per_flash) {

	int i, j;
	/* Init operations counters variable */
	ssd->physical_page_writes = 0;
	ssd->logical_page_writes = 0;
	ssd->physical_page_reads = 0;
	ssd->logical_page_reads = 0;
	ssd->erases = 0;
	ssd->occupied_pages = 0;
	/* Init write / read delay */
	ssd->write_delay = 0;
	ssd->read_delay = 0;

	/* Init ssd block objects */
	ssd->blocks = (int **)malloc(sizeof(ssd_block*) * flash_nb);
	for( i = 0; i < flash_nb; i++ ) {
		*(ssd->blocks + i) = (ssd_block*)malloc(sizeof(ssd_block)*block_nb);
		for( j = 0; j < block_nb; j++ )
			_SSD_BLOCK_INIT(&(ssd->blocks[i][j]));
	}


	/* Init channel opjects */
	ssd->channels = (ssd_channel*) malloc(sizeof(ssd_channel) * channel_nb);
	for( i = 0; i < channel_nb; i++ ) {
		_SSD_CHANNEL_INIT(&(ssd->channels[i]));
	}

	/* Init flash status & timestamp */
	ssd->planes = (ssd_plane *)malloc(sizeof(ssd_plane) * flash_nb * planes_per_flash);
	for( i = 0; i < flash_nb * planes_per_flash; i++ ) {
		_SSD_PLANE_INIT(&(ssd->planes[i]));
	}
}

void _SSD_BLOCK_INIT(ssd_block* block)
{
	block->occupied_pages_counter = 0;
}

void _SSD_CHANNEL_INIT(ssd_channel* channel)
{
	channel->status = CHANNEL_IS_EMPTY;
	channel->timer = -1;
	channel->register_timer = -1;
	channel->op_nb = 0;
	channel->done_op_nb = 0;
}

void _SSD_PLANE_INIT(ssd_plane* plane)
{
	plane->status = FLASH_READY;
	plane->timer = -1;
}

// busy wait until @timestamp
void _SSD_BUSY_WAIT(int64_t until_timestamp)
{
	int64_t now = get_usec();
	while( now < until_timestamp ){
		now = get_usec();
	}
}

int SSD_IO_INIT(void){

	int i= 0;
	int j= 0;
	int k= 0;

	/* Print SSD version */
	PINFO("SSD Emulator Version: %s ver. (%s)\n", ssd_version, ssd_date);
	_SSD_DISK_INIT(&ssd, FLASH_NB, BLOCK_NB, CHANNEL_NB, PLANES_PER_FLASH);
	return 0;
}

int SSD_IO_TERM(void)
{
	int i, j;

	for(i=0; i< FLASH_NB; i++)
		free(ssd.blocks[i]);
	free(ssd.blocks);

	free(ssd.channels);
	free(ssd.planes);
	return 0;
}

// IO wait - calculate and wait for time were
// block @block_nb on flash @flash_nb will
// be able for execute new operation
void _SSD_IO_WAIT(unsigned int flash_nb, unsigned int block_nb)
{
	int plane = CALC_PLANE_BY_FLASH_BLOCK(flash_nb, block_nb);
	_SSD_BUSY_WAIT(ssd.planes[plane].timer);
}

// Channel access
void _SSD_CHANNEL_ACCESS(int op_nb, int channel)
{
	int i = 0;
	while(ssd.channels[channel].done_op_nb + 1 != op_nb);
	_SSD_BUSY_WAIT(ssd.channels[channel].timer);
}

void _SSD_CHANNEL_ACCESS_REAL(int channel)
{
	_SSD_BUSY_WAIT(ssd.channels[channel].register_timer);
}

// Register write
void _SSD_REGISTER_WRITE(int op_nb, unsigned int flash_nb)
{
	int channel = flash_nb % CHANNEL_NB;
	_SSD_CHANNEL_ACCESS(op_nb, channel);
	ssd.channels[channel].op_nb = op_nb;

	int delay = REG_WRITE_DELAY + CELL_PROGRAM_DELAY;
	if( ssd.channels[channel].status == CHANNEL_IS_READ ) {
		delay += CHANNEL_SWITCH_DELAY_W;
	}
	ssd.channels[channel].status = CHANNEL_IS_WRITE;
	ssd.channels[channel].timer = get_usec() + delay;
	ssd.channels[channel].register_timer = ssd.channels[channel].timer - CELL_PROGRAM_DELAY;
}

void _SSD_SET_BLOCK_CELL_PROGRAMMING(unsigned int flash_nb, unsigned int block_nb)
{
	_SSD_IO_WAIT(flash_nb, block_nb);
	int plane = CALC_PLANE_BY_FLASH_BLOCK(flash_nb, block_nb);
	ssd.planes[plane].status = FLASH_PROGRAM;
	ssd.planes[plane].timer = get_usec() + CELL_PROGRAM_DELAY;
}


void _SSD_SET_REGISTER_READ(int op_nb, unsigned int flash_nb)
{
	int channel = flash_nb % CHANNEL_NB;
	// wait for channel can be accessed
	_SSD_CHANNEL_ACCESS(op_nb, channel);
	int delay = REG_READ_DELAY + CELL_READ_DELAY;
	// switch channel delay for not in read mode channel
	if( ssd.channels[channel].status != CHANNEL_IS_READ )
		delay += CHANNEL_SWITCH_DELAY_R;
	// set channel mode to read
	ssd.channels[channel].status = CHANNEL_IS_READ;
	// measure channel operation start time
	ssd.channels[channel].timer = get_usec() + delay;
	ssd.channels[channel].register_timer = ssd.channels[channel].timer - CELL_READ_DELAY;
}

void _SSD_SET_BLOCK_READ(unsigned int flash_nb, unsigned int block_nb)
{
	_SSD_IO_WAIT(flash_nb, block_nb);
	int plane = CALC_PLANE_BY_FLASH_BLOCK(flash_nb, block_nb);
	// set flash plane as on cell read operation
	ssd.planes[plane].status = FLASH_READ;
	ssd.planes[plane].timer = get_usec() + CELL_READ_DELAY;
}


void _SSD_SET_BLOCK_ERASE(unsigned int flash_nb, unsigned int block_nb)
{
	_SSD_IO_WAIT(flash_nb, block_nb);

	int plane = CALC_PLANE_BY_FLASH_BLOCK(flash_nb, block_nb);
	// set flash plane as on erase operation
	ssd.planes[plane].status = FLASH_ERASE;
	ssd.planes[plane].timer = get_usec() + BLOCK_ERASE_DELAY;
}


//MIX
int64_t qemu_overhead;

void _SSD_SET_BLOCK_PAGE_COPYBACK(unsigned int flash_nb, unsigned int block_nb)
{
	_SSD_IO_WAIT(flash_nb, block_nb);
	int plane = CALC_PLANE_BY_FLASH_BLOCK(flash_nb, block_nb);
	// set flash plane as on erase operation
	ssd.planes[plane].status = FLASH_ERASE;
	ssd.planes[plane].timer = get_usec();
}


unsigned long int SSD_GET_PHYSICAL_PAGE_WRITE_COUNT(void)
{
	return ssd.physical_page_writes;
}
unsigned long int SSD_GET_LOGICAL_PAGE_WRITE_COUNT(void)
{
	return ssd.logical_page_writes;
}
unsigned long int SSD_GET_LOGICAL_PAGE_READ_COUNT(void)
{
	return ssd.logical_page_reads;
}
unsigned long int SSD_GET_PHYSICAL_PAGE_READ_COUNT(void)
{
	return ssd.physical_page_reads;
}
unsigned long int SSD_GET_ERASE_COUNT(void)
{
	return ssd.erases;
}

double SSD_UTIL_CALC(void)
{
	return (double)ssd.occupied_pages / PAGES_IN_SSD;
}

// return average of logical write delay time
double SSD_WRITE_BANDWIDTH(void)
{
	if( ssd.write_delay == 0 ) return 0.0;
	return GET_IO_BANDWIDTH((double)ssd.write_delay / ssd.logical_page_writes);
}
// return average of logical read delay time
double SSD_READ_BANDWIDTH(void)
{
	if( ssd.read_delay == 0 ) return 0.0;
	return GET_IO_BANDWIDTH((double)ssd.read_delay / ssd.logical_page_reads);
}


unsigned long SSD_TOTAL_WRITE_DELAY(void)
{
	return ssd.write_delay;
}
unsigned long SSD_TOTAL_READ_DELAY(void)
{
	return ssd.read_delay;
}


