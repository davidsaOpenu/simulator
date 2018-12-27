// Copyright(c)2013
//
// Hanyang University, Seoul, Korea
// Embedded Software Systems Lab. All right reserved

#include "common.h"

int64_t io_alloc_overhead=0;

char ssd_version[4] = "1.0";
char ssd_date[9] = "13.04.11";

struct timeval logging_parser_tv;

// physical page writes counter
int physical_page_write = 0;

// logical page writes counter
int logical_page_write = 0;

// logical page read counter
int logical_page_read = 0;
//
// number of current pages in use in blocks
// by flash number and block number.
// e.g number of pages in use at flash i,
// block j is block_pages_counter[i][j]
int** block_pages_counter;
//
// sum of all delay time for logical write operation
// in usec (microseconds)
unsigned long int write_delay_sum = 0;
//
// sum of all delay time for logical read operation
// in usec (microseconds)
unsigned long int read_delay_sum = 0;
//
// status of register by channel number
// & status changed timestamp
int* channel_status;
int64_t* channel_timer;
//
// status of flash plane by flash and block number
// & status changed timestamp
int* flash_status;
int64_t* flash_timer;
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

// busy wait until @timestamp
void _SSD_BUSY_WAIT(int64_t until_timestamp) {

	int64_t now = get_usec();
	while( now < until_timestamp ){
		now = get_usec();
	}
}


int SSD_IO_INIT(void){

	int i= 0;
	int j= 0;

	/* Print SSD version */
	PINFO("SSD Emulator Version: %s ver. (%s)\n", ssd_version, ssd_date);

	/* Init page write / read counters variable */
	physical_page_write = 0;
	logical_page_write = 0;
	logical_page_read = 0;

	/* Init write / read delay */
	write_delay_sum = 0;
	read_delay_sum = 0;

	/* Init pages in block counter */
	block_pages_counter = (int **)malloc(sizeof(int*) * FLASH_NB * BLOCK_NB);
	for( i = 0; i < FLASH_NB; i++ ) {
		*(block_pages_counter + i) = (int*)malloc(sizeof(int)*BLOCK_NB);
		for( j = 0; j < BLOCK_NB; j++ )
			block_pages_counter[i][j] = 0;
	}

	/* Init channel status & timestamp */
	channel_status = (int *)malloc(sizeof(int) * CHANNEL_NB);
	channel_timer = (int64_t *)malloc(sizeof(int64_t) * CHANNEL_NB);
	for( i = 0; i < CHANNEL_NB; i++ ) {
		// initiate channel status to empty
		*(channel_status + i) = CHANNEL_IS_EMPTY;
		*(channel_timer + i) = -1;
	}

	/* Init flash status & timestamp */
	flash_status = (int *)malloc(sizeof(int) * FLASH_NB * PLANES_PER_FLASH);
	flash_timer = (int64_t *)malloc(sizeof(int64_t) * FLASH_NB * PLANES_PER_FLASH);
	for( i = 0; i < FLASH_NB * PLANES_PER_FLASH; i++ ) {
		// initiate flash plane status to ready state
		*(flash_status + i) = FLASH_READY;
		*(flash_timer + i) = -1;
	}

	return 0;
}

int SSD_IO_TERM(void)
{
	int i;
	for(i=0; i< FLASH_NB; i++)
		free(block_pages_counter[i]);
	free(block_pages_counter);
	free(channel_status);
	free(channel_timer);
	free(flash_status);
	free(flash_timer);
	return 0;
}

// IO wait - calculate and wait for time were
// block @block_nb on flash @flash_nb will
// be able for execute new operation
void _SSD_IO_WAIT(unsigned int flash_nb, unsigned int block_nb) {

	int plane = CALC_PLANE_BY_FLASH_BLOCK(flash_nb, block_nb);
	// initiate wait until timestamp
	int64_t wait_until = 0;

	//
	// calculate timestamp to plane get available
	if( flash_status[plane] == FLASH_PROGRAM )
		// block on cell program operation
		wait_until = flash_timer[plane] + CELL_PROGRAM_DELAY;
	if( flash_status[plane] == FLASH_READ )
		// block on cell read operation
		wait_until = flash_timer[plane] + CELL_READ_DELAY;
	if( flash_status[plane] == FLASH_ERASE )
		// block on erase operation
		wait_until = flash_timer[plane] + BLOCK_ERASE_DELAY;
	if( flash_status[plane] == FLASH_COPYBACK )
		// page copy back operation
		wait_until = flash_timer[plane] + CELL_READ_DELAY + CELL_PROGRAM_DELAY;
	// execute busy waiting until @wait_until
	_SSD_BUSY_WAIT(wait_until);
}

// Channel access
void _SSD_CHANNEL_ACCESS(int channel) {

	int64_t wait_until;
	if( channel_status[channel] == CHANNEL_IS_EMPTY ) {
		return;
	}

	//
	// calculate timestamp to channel access get available
	if( channel_status[channel] == CHANNEL_IS_WRITE ) {
		wait_until = channel_timer[channel] + REG_WRITE_DELAY;
	} else if( channel_status[channel] == CHANNEL_IS_READ ) {
		wait_until = channel_timer[channel] + REG_READ_DELAY;
	}
	// execute busy waiting until @wait_until
	_SSD_BUSY_WAIT(wait_until);
}


// Register write
void _SSD_REGISTER_WRITE(unsigned int flash_nb) {

	int channel = flash_nb % CHANNEL_NB;
	_SSD_CHANNEL_ACCESS(channel);
	if( channel_status[channel] != CHANNEL_IS_WRITE )
		_SSD_BUSY_WAIT(get_usec() + CHANNEL_SWITCH_DELAY_W);
	channel_status[channel] = CHANNEL_IS_WRITE;
	channel_timer[channel] = get_usec();
}


void _SSD_SET_BLOCK_CELL_PROGRAMMING(unsigned int flash_nb, unsigned int block_nb)
{
	_SSD_IO_WAIT(flash_nb, block_nb);
	int plane = CALC_PLANE_BY_FLASH_BLOCK(flash_nb, block_nb);
	flash_status[plane] = FLASH_PROGRAM;
	flash_timer[plane] = get_usec();
}



ftl_ret_val SSD_PAGE_WRITE(unsigned int flash_nb, unsigned int block_nb, unsigned int page_nb, int offset, int type, int io_page_nb)
{
	int channel = flash_nb % CHANNEL_NB;

	// measure start time
	int64_t start_usec = get_usec();
    TIME_MICROSEC(_start);
    // register write
    _SSD_REGISTER_WRITE(flash_nb);
	// wait for register write done
	_SSD_CHANNEL_ACCESS(channel);
	// cell programming
	_SSD_SET_BLOCK_CELL_PROGRAMMING(flash_nb, block_nb);
	// wait for cell programming done
	_SSD_IO_WAIT(flash_nb, block_nb);
	// end of delay time
    TIME_MICROSEC(_end);
	write_delay_sum += (get_usec() - start_usec);
	// page (physical) writes counter increase
    physical_page_write++;
    // number of in use pages on block @block_nb
    // on flash @flash_nb counter
    block_pages_counter[flash_nb][block_nb]++;
    // on logical write increase logical
    // page write counter
    if( type == WRITE )
    	logical_page_write++;
    //
    // send logs to monitor (old):
    // write page, write amplification,
    // write bandwidth, SSD util
	WRITE_LOG("WRITE PAGE %d\n", 1);
    WRITE_LOG("WB AMP %lf\n", (double)physical_page_write / logical_page_write);
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


void _SSD_SET_REGISTER_READ(unsigned int flash_nb) {

	int channel = flash_nb % CHANNEL_NB;
	// wait for channel can be accessed
	_SSD_CHANNEL_ACCESS(channel);
	// switch channel delay for not in read mode channel
	if( channel_status[channel] != CHANNEL_IS_READ )
		_SSD_BUSY_WAIT(get_usec() + CHANNEL_SWITCH_DELAY_R);
	// set channel mode to read
	channel_status[channel] = CHANNEL_IS_READ;
	// measure channel operation start time
	channel_timer[channel] = get_usec();
}

void _SSD_SET_BLOCK_READ(unsigned int flash_nb, unsigned int block_nb)
{
	_SSD_IO_WAIT(flash_nb, block_nb);
	int plane = CALC_PLANE_BY_FLASH_BLOCK(flash_nb, block_nb);
	// set flash plane as on cell read operation
	flash_status[plane] = FLASH_READ;
	flash_timer[plane] = get_usec();
}

ftl_ret_val SSD_PAGE_READ(unsigned int flash_nb, unsigned int block_nb, unsigned int page_nb, int offset, int type, int io_page_nb)
{
	int channel = flash_nb % CHANNEL_NB;

	// measure start delay time
	int64_t start_usec = get_usec();
    TIME_MICROSEC(_start);

    _SSD_SET_REGISTER_READ(flash_nb);
    _SSD_CHANNEL_ACCESS(channel);
    _SSD_SET_BLOCK_READ(flash_nb, block_nb);
    _SSD_IO_WAIT(flash_nb, block_nb);

    // measure end delay time
    TIME_MICROSEC(_end);
    int64_t duration = get_usec() - start_usec;
    if( type == READ ) {
    	// count duration as read delay
    	// on logical page read
    	read_delay_sum += duration;
    	logical_page_read++;
    } else if( type == GC_READ )
    	// count duration as read delay
    	// on logical GC page read
    	write_delay_sum += duration;
    // send log to new monitor
	LOG_PHYSICAL_CELL_READ(GET_LOGGER(flash_nb), (PhysicalCellReadLog) {
	    .channel = channel, .block = block_nb, .page = page_nb,
        .metadata.logging_start_time = _start,
        .metadata.logging_end_time = _end
	});

	return FTL_SUCCESS;
}

void _SSD_SET_BLOCK_ERASE(unsigned int flash_nb, unsigned int block_nb)
{
	_SSD_IO_WAIT(flash_nb, block_nb);

	int plane = CALC_PLANE_BY_FLASH_BLOCK(flash_nb, block_nb);
	// set flash plane as on erase operation
	flash_status[plane] = FLASH_ERASE;
	flash_timer[plane] = get_usec();
}

ftl_ret_val SSD_BLOCK_ERASE(unsigned int flash_nb, unsigned int block_nb)
{
	int channel;
	// measure start delay time
	int64_t start_usec = get_usec();
    TIME_MICROSEC(_start);

    _SSD_SET_BLOCK_ERASE(flash_nb, block_nb);
    _SSD_IO_WAIT(flash_nb, block_nb);

    // measure end delay time
    TIME_MICROSEC(_end);
    // count erase operations delay as write delay
	write_delay_sum += (get_usec() - start_usec);
	// set number of pages as 0
    block_pages_counter[flash_nb][block_nb] = 0;
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

//MIX
int64_t qemu_overhead;


void _SSD_SET_BLOCK_PAGE_COPYBACK(unsigned int flash_nb, unsigned int block_nb)
{
	_SSD_IO_WAIT(flash_nb, block_nb);
	int plane = CALC_PLANE_BY_FLASH_BLOCK(flash_nb, block_nb);
	// set flash plane as on erase operation
	flash_status[plane] = FLASH_ERASE;
	flash_timer[plane] = get_usec();
}

ftl_ret_val SSD_PAGE_COPYBACK(uint32_t source, uint32_t destination, int type){

	int source_plane, destination_plane;
	int flash_nb, block_nb, channel;
	//Check source and destination pages are at the same plane.
	source_plane = CALC_PLANE(source);
	destination_plane = CALC_PLANE(destination);

	if (source_plane != destination_plane)
		//copyback from different planes is not supported
		return FTL_FAILURE;

	flash_nb = CALC_FLASH(source);
	block_nb = CALC_BLOCK(source);
	channel = flash_nb % CHANNEL_NB;
	// measure start delay time
	int64_t start_usec = get_usec();

    _SSD_SET_REGISTER_READ(flash_nb);
    _SSD_CHANNEL_ACCESS(channel);
    _SSD_SET_BLOCK_PAGE_COPYBACK(flash_nb, block_nb);
    _SSD_IO_WAIT(flash_nb, block_nb);

    // count page copy back operation delay as write delay
	write_delay_sum += (get_usec() - start_usec);

	return FTL_SUCCESS;
}



double SSD_UTIL_CALC(void)
{
	int in_use_pages = 0;
	int i, j;
	// count number of pages in use
	// by page in block counter
	for( i = 0; i < FLASH_NB; i++ ) {
		for( j = 0; j < BLOCK_NB; j++ ) {
			in_use_pages += block_pages_counter[i][j];
		}
	}
	return (double)in_use_pages / PAGES_IN_SSD;
}

// return average of logical write delay time
double SSD_WRITE_BANDWIDTH(void)
{
	if( write_delay_sum == 0 ) return 0.0;
	return GET_IO_BANDWIDTH((double)write_delay_sum / logical_page_write);
}
// return average of logical read delay time
double SSD_READ_BANDWIDTH(void)
{
	if( read_delay_sum == 0 ) return 0.0;
	return GET_IO_BANDWIDTH((double)read_delay_sum / logical_page_read);
}
