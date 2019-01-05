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
int physical_page_write = 0; // physical page writes counter
int logical_page_write = 0;	 // logical page writes counter
int physical_page_read = 0;	 // physical page read
int logical_page_read = 0;	 // logical page read counter
int erase_counter = 0;		 // erase counter
//
// counter of all occupied pages in ssd.
// used for SSD utils calculate,
// formula: ssd_utils = @occupied_page_counter / @PAGES_IN_SSD
int occupied_page_counter = 0;
//
// block object by flash number and block number.
// e.g number of pages in use at flash i,
// block j is blocks[i][j]
ssd_block** blocks;
//
// sum of all delay time for logical write operation
// in usec (microseconds)
unsigned long int write_delay_sum = 0;
//
// sum of all delay time for logical read operation
// in usec (microseconds)
unsigned long int read_delay_sum = 0;
//
// page free / occupied indicator
// used for ssd operation success / failure indication
// e.g page @k on block @j on flash @i,
// if block_pages_counter[i][j][k] == 0 : page is free
// if block_pages_counter[i][j][k] == 1 : page is occupied
//int*** page_map;
//
// status of register by channel number
// & status changed timestamp
int* channel_status;
int64_t* channel_timer;
int64_t* channel_timer_real;
int* channel_lock_nb;
int* channel_last_op_nb;
int op_nb;
int op_lock = 0;
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

pthread_mutex_t* channel_mutex;// = PTHREAD_MUTEX_INITIALIZER;

void _SSD_BLOCK_INIT(ssd_block* block) {
	block->occupied_pages_counter = 0;
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
	int k= 0;

	/* Print SSD version */
	PINFO("SSD Emulator Version: %s ver. (%s)\n", ssd_version, ssd_date);

	/* Init page write / read counters variable */
	physical_page_write = 0;
	logical_page_write = 0;
	physical_page_read = 0;
	logical_page_read = 0;
	occupied_page_counter = 0;
	erase_counter = 0;

	/* Init write / read delay */
	write_delay_sum = 0;
	read_delay_sum = 0;

	/* Init ssd block objects */
	blocks = (int **)malloc(sizeof(ssd_block*) * FLASH_NB);
	for( i = 0; i < FLASH_NB; i++ ) {
		*(blocks + i) = (ssd_block*)malloc(sizeof(ssd_block)*BLOCK_NB);
		for( j = 0; j < BLOCK_NB; j++ )
			_SSD_BLOCK_INIT(&(blocks[i][j]));
	}


	/* Init channel status & timestamp */
	channel_status = (int *)malloc(sizeof(int) * CHANNEL_NB);
	channel_timer = (int64_t *)malloc(sizeof(int64_t) * CHANNEL_NB);
	channel_timer_real = (int64_t *)malloc(sizeof(int64_t) * CHANNEL_NB);
	channel_lock_nb = (int *)malloc(sizeof(int) * CHANNEL_NB);
//	pthread_mutex_t* channel_mutex
	channel_mutex = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t) * CHANNEL_NB);
	channel_last_op_nb = (int *)malloc(sizeof(int) * CHANNEL_NB);
	op_lock = 0;
	for( i = 0; i < CHANNEL_NB; i++ ) {
		// initiate channel status to empty
		*(channel_status + i) = CHANNEL_IS_EMPTY;
		*(channel_timer + i) = -1;
		*(channel_timer_real + i) = -1;
		*(channel_lock_nb + i) = 0;
//		*(channel_mutex + i) = PTHREAD_MUTEX_INITIALIZER;
		*(channel_last_op_nb + i) = 0;
	}

	/* Init flash status & timestamp */
	flash_status = (int *)malloc(sizeof(int) * FLASH_NB * PLANES_PER_FLASH);
	flash_timer = (int64_t *)malloc(sizeof(int64_t) * FLASH_NB * PLANES_PER_FLASH);
	for( i = 0; i < FLASH_NB * PLANES_PER_FLASH; i++ ) {
		// initiate flash plane status to ready state
		*(flash_status + i) = FLASH_READY;
		*(flash_timer + i) = -1;
	}

	/* Init page in use map */
//	page_map = (int ***)malloc(sizeof(int**) * FLASH_NB);
//	for( i = 0; i < FLASH_NB; i++ ) {
//		page_map[i] = (int**)malloc(sizeof(int*)*BLOCK_NB);
//		for( j = 0; j < BLOCK_NB; j++ ) {
//			page_map[i][j] = (int*)malloc(sizeof(int)*PAGE_NB);
//			for( k = 0; k < PAGE_NB; k++ )
//				// set page @k on block @j on flash @i as free
//				page_map[i][j][k] = 0;
//		}
//	}
	return 0;
}

int SSD_IO_TERM(void)
{
	int i, j;

	for(i=0; i< FLASH_NB; i++)
		free(blocks[i]);
	free(blocks);

	free(channel_status);
	free(channel_timer);
	free(channel_timer_real);
	free(channel_lock_nb);
	free(channel_last_op_nb);
	free(channel_mutex);
	free(flash_status);
	free(flash_timer);
	return 0;
}

// IO wait - calculate and wait for time were
// block @block_nb on flash @flash_nb will
// be able for execute new operation
void _SSD_IO_WAIT(unsigned int flash_nb, unsigned int block_nb)
{

	int plane = CALC_PLANE_BY_FLASH_BLOCK(flash_nb, block_nb);
	_SSD_BUSY_WAIT(flash_timer[plane]);
}

int64_t s_2;
int64_t s_3;

// Channel access
void _SSD_CHANNEL_ACCESS(int op_nb, int channel)
{
	int i = 0;
	int *p = &channel_last_op_nb[channel];
	int p2 = *p;
//	printf("channel_last_op_nb[channel] + 1 != op_nb %d\n", channel_last_op_nb[channel] + 1 != op_nb);
//	printf("channel: %d, in *p + 1 (%d) vs op_nb (%d)\n", channel, *p + 1, op_nb);
	while(p2 + 1 != op_nb){
		if( i++ % 10000 == 0 ) {
//			printf("bet *p + 1 (%d) vs op_nb (%d)\n", *p + 1, op_nb);
//			printf("channel_last_op_nb[channel] + 1 != op_nb: %d\n", channel_last_op_nb[channel] + 1 != op_nb);
//			printf("channel_last_op_nb[channel] + 1 == op_nb: %d\n", channel_last_op_nb[channel] + 1 == op_nb);
		}
		p2 = *p;
		if( op_nb == 2 )
			s_2 = get_usec();
		if( op_nb == 3 )
			s_3 = get_usec();
	}


//	printf("out *p + 1 (%d) vs op_nb (%d)\n", *p + 1, op_nb);
//	printf("%d %d %d\n", op_lock, channel_lock_nb[channel], op_nb);
//	int i = 0;
//	while( op_lock && channel_lock_nb[channel] < op_nb ) {
//		if( i++ % 100000 )
//			printf("%d %d %d\n", op_lock, channel_lock_nb[channel], op_nb);
//	}
//	printf("%ld vs %ld\n", channel_timer[channel], get_usec());
//	int64_t start = get_usec();
//    pthread_mutex_unlock(channel_mutex[channel]);

	_SSD_BUSY_WAIT(channel_timer[channel]);
//    pthread_mutex_unlock(&mutex);
//    printf("mutex: %d\n\n", get_usec() - start);
}

void _SSD_CHANNEL_ACCESS_REAL(int channel)
{
	_SSD_BUSY_WAIT(channel_timer_real[channel]);
}

// Register write
void _SSD_REGISTER_WRITE(int op_nb, unsigned int flash_nb)
{
	int channel = flash_nb % CHANNEL_NB;
	_SSD_CHANNEL_ACCESS(op_nb, channel);
	channel_timer[channel] = op_nb;
	op_lock = 1;
//    pthread_mutex_lock(&channel_mutex[channel]);

	int delay = REG_WRITE_DELAY + CELL_PROGRAM_DELAY;
	if( channel_status[channel] == CHANNEL_IS_READ ) {
		delay += CHANNEL_SWITCH_DELAY_W;
	}
	channel_status[channel] = CHANNEL_IS_WRITE;
	channel_timer[channel] = get_usec() + delay;
	channel_timer_real[channel] = channel_timer[channel] - CELL_PROGRAM_DELAY;
}

void _SSD_SET_BLOCK_CELL_PROGRAMMING(unsigned int flash_nb, unsigned int block_nb)
{
	_SSD_IO_WAIT(flash_nb, block_nb);
	int plane = CALC_PLANE_BY_FLASH_BLOCK(flash_nb, block_nb);
	flash_status[plane] = FLASH_PROGRAM;
	flash_timer[plane] = get_usec() + CELL_PROGRAM_DELAY;
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

    __atomic_fetch_add(&channel_lock_nb[channel], 1, __ATOMIC_SEQ_CST);
	int current_op_nb = channel_lock_nb[channel];
//	printf("current_op_nb: %d\n", current_op_nb);
//	printf("channel_lock_nb[channel]: %d\n", channel_lock_nb[channel]);
//	printf("PAGE_WRITE(%d,%d,%d) channel: %d, op: %d, start_usec: %ld \n"
//			,flash_nb, block_nb, page_nb, channel, current_op_nb, get_usec());

	/**
	 * Delay execution & measurement
	 */
	// measure start time
	int64_t start_usec = get_usec();
    TIME_MICROSEC(_start);
    // register write
    _SSD_REGISTER_WRITE(current_op_nb, flash_nb);
	int64_t after_register_write = get_usec();
	int64_t after_register_channel_timer = channel_timer[channel];
	// wait for register write done
    _SSD_CHANNEL_ACCESS_REAL(channel);
	int64_t after_register_write_delay = get_usec();
	// cell programming
	_SSD_SET_BLOCK_CELL_PROGRAMMING(flash_nb, block_nb);
	int64_t after_cell_program_start = get_usec();
	// wait for cell programming done
	_SSD_IO_WAIT(flash_nb, block_nb);
    __atomic_fetch_add(&channel_last_op_nb[channel], 1, __ATOMIC_SEQ_CST);
//    printf("channel_last_op_nb[channel]: %d\n", channel_last_op_nb[channel]);
	op_lock = 0;
	// end of delay time
    TIME_MICROSEC(_end);
	int64_t done_usec = get_usec();
	int plane = CALC_PLANE_BY_FLASH_BLOCK(flash_nb, block_nb);

	unsigned int delay = (get_usec() - start_usec);

//    pthread_mutex_unlock(&channel_mutex[channel]);
    int64_t mutex_done = get_usec();

//    printf("start_usec: %ld\n", start_usec );
//    printf("after_register_write: %ld (%d)\n", after_register_write, after_register_write - start_usec);
//    printf("channel_timer[channel] %ld (%ld)\n", after_register_channel_timer, after_register_channel_timer - after_register_write );
//    printf("after_register_write_delay %ld (%d)\n", after_register_write_delay, after_register_write_delay - after_register_write );
//    printf("flash_timer[plane]: %ld \n", flash_timer[plane] );
//    printf("after_cell_program_start: %ld (%d)\n", after_cell_program_start, after_cell_program_start - after_register_write_delay );
//    printf("done_usec: %ld (%d %d)\n", done_usec, done_usec - after_register_write, done_usec - start_usec );
//    printf("mutex_done: %ld (%d)\n", mutex_done, mutex_done - done_usec);
//    printf("delay: %d\n\n\n", delay);


	/**
	 * Statistics update
	 */
	write_delay_sum += delay;
	// page (physical) writes counter increase
    physical_page_write++;
    // page occupy, update occupied pages counter
    occupied_page_counter++;
    // number of in use pages on block @block_nb
    // on flash @flash_nb counter
    (blocks[flash_nb][block_nb].occupied_pages_counter)++;
    // on logical write increase logical
    // page write counter
    if( type == WRITE )
    	logical_page_write++;
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

void _SSD_SET_REGISTER_READ(int op_nb, unsigned int flash_nb)
{
	int channel = flash_nb % CHANNEL_NB;
	// wait for channel can be accessed
	_SSD_CHANNEL_ACCESS(op_nb, channel);
	int delay = REG_READ_DELAY + CELL_READ_DELAY;
	// switch channel delay for not in read mode channel
	if( channel_status[channel] != CHANNEL_IS_READ )
		delay += CHANNEL_SWITCH_DELAY_R;
	// set channel mode to read
	channel_status[channel] = CHANNEL_IS_READ;
	// measure channel operation start time
	channel_timer[channel] = get_usec() + delay;
	channel_timer_real[channel] = channel_timer[channel] - CELL_READ_DELAY;
}

void _SSD_SET_BLOCK_READ(unsigned int flash_nb, unsigned int block_nb)
{
	_SSD_IO_WAIT(flash_nb, block_nb);
	int plane = CALC_PLANE_BY_FLASH_BLOCK(flash_nb, block_nb);
	// set flash plane as on cell read operation
	flash_status[plane] = FLASH_READ;
	flash_timer[plane] = get_usec() + CELL_READ_DELAY;
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

    int64_t fct = channel_timer[channel];

    __atomic_fetch_add(&channel_lock_nb[channel], 1, __ATOMIC_SEQ_CST);
	int current_op_nb = channel_lock_nb[channel];

//	printf("channel: %d, op: %d, start_usec: %ld PAGE_READ(%d,%d,%d)\n"
//			, channel, current_op_nb, get_usec(), flash_nb, block_nb, page_nb);

    int64_t reg_read_start = get_usec();
    _SSD_SET_REGISTER_READ(current_op_nb, flash_nb);
    int64_t ct = channel_timer[channel];
    int64_t ct_real = channel_timer_real[channel];
    int64_t reg_read_done = get_usec();
    _SSD_CHANNEL_ACCESS_REAL(channel);
    int64_t cell_read_start = get_usec();
    _SSD_SET_BLOCK_READ(flash_nb, block_nb);
    _SSD_IO_WAIT(flash_nb, block_nb);
    int64_t cell_read_done = get_usec();
    __atomic_fetch_add(&channel_last_op_nb[channel], 1, __ATOMIC_SEQ_CST);

    // measure end delay time
    TIME_MICROSEC(_end);
    int64_t duration = get_usec() - start_usec;
//    printf("fct: %ld\n", fct);
//    printf("ct: %ld\n", ct);
//    printf("ct_real: %ld\n", ct_real);
//    printf("reg read start: %ld\n", reg_read_start);
//    printf("reg read done: %ld\n", reg_read_done);
//    printf("cell read start: %ld\n", cell_read_start);
//    printf("cell read done: %ld\n", cell_read_done);

	/**
	 * Statistics update
	 */
	physical_page_read++;
    if( type == READ ) {
    	// count duration as read delay
    	// on logical page read
    	read_delay_sum += duration;
    	logical_page_read++;
    } else if( type == GC_READ ) {
    	// count duration as read delay
    	// on logical GC page read
    	write_delay_sum += duration;
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

void _SSD_SET_BLOCK_ERASE(unsigned int flash_nb, unsigned int block_nb)
{
	_SSD_IO_WAIT(flash_nb, block_nb);

	int plane = CALC_PLANE_BY_FLASH_BLOCK(flash_nb, block_nb);
	// set flash plane as on erase operation
	flash_status[plane] = FLASH_ERASE;
	flash_timer[plane] = get_usec() + BLOCK_ERASE_DELAY;
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

    __atomic_fetch_add(&channel_lock_nb[channel], 1, __ATOMIC_SEQ_CST);
	int current_op_nb = channel_lock_nb[channel];

//	printf("channel: %d, op: %d, start_usec: %ld SSD_BLOCK_ERASE(%d,%d)\n"
//			, channel, current_op_nb, get_usec(), flash_nb, block_nb);

	int64_t before_channel_access = get_usec();
    _SSD_CHANNEL_ACCESS(current_op_nb, channel);
	int64_t after_channel_access = get_usec();
    __atomic_fetch_add(&channel_last_op_nb[channel], 1, __ATOMIC_SEQ_CST);
    _SSD_SET_BLOCK_ERASE(flash_nb, block_nb);
	int64_t after_set_erase = get_usec();
    _SSD_IO_WAIT(flash_nb, block_nb);
	int64_t after_erase = get_usec();

    // measure end delay time
    TIME_MICROSEC(_end);
    // count erase operations delay as write delay
	unsigned int delay = (get_usec() - start_usec);

//	printf("start_usec: %ld\n", start_usec );
//	printf("before_channel_access: %ld\n", before_channel_access );
//	printf("after_channel_access: %ld (%d)\n", after_channel_access, after_channel_access - before_channel_access);
//	printf("after_set_erase %ld (%ld)\n", after_set_erase, after_set_erase - after_channel_access );
//	printf("after_erase %ld (%d)\n", after_erase, after_erase - after_set_erase );
//    printf("delay: %d\n", delay);
//	printf("\n\n\n");

	/**
	 * Statistics update
	 */
	write_delay_sum += delay;
    // occupied page in block released, update occupied pages counter
	occupied_page_counter -= blocks[flash_nb][block_nb].occupied_pages_counter;
	// calculate current occupied pages counter
	// and set number of pages as 0
    blocks[flash_nb][block_nb].occupied_pages_counter = 0;
    // set block's pages as free
//    for( i = 0; i < PAGE_NB; i++ ) {
//    	page_map[flash_nb][block_nb][i] = 0;
//    }
    // increase erase counter
    erase_counter++;

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

    __atomic_fetch_add(&channel_lock_nb[channel], 1, __ATOMIC_SEQ_CST);
	int current_op_nb = channel_lock_nb[channel];

//	printf("SSD_PAGE_COPYBACK(%d,%d) channel: %d, op: %d, start_usec: %ld \n"
//			,flash_nb, block_nb, channel, current_op_nb, get_usec());


	/**
	 * Delay execution & measurement
	 */
	// measure start delay time
	int64_t start_usec = get_usec();

    _SSD_SET_REGISTER_READ(current_op_nb, flash_nb);
    _SSD_CHANNEL_ACCESS(current_op_nb, channel);
    _SSD_SET_BLOCK_PAGE_COPYBACK(flash_nb, block_nb);
    _SSD_IO_WAIT(flash_nb, block_nb);
    __atomic_fetch_add(&channel_last_op_nb[channel], 1, __ATOMIC_SEQ_CST);

    // count page copy back operation delay as write delay
	unsigned int delay = (get_usec() - start_usec);

	/**
	 * Statistics update
	 */
	write_delay_sum += delay;

	return FTL_SUCCESS;
}

unsigned long int SSD_GET_PHYSICAL_PAGE_WRITE_COUNT(void)
{
	return physical_page_write;
}
unsigned long int SSD_GET_LOGICAL_PAGE_WRITE_COUNT(void)
{
	return logical_page_write;
}
unsigned long int SSD_GET_LOGICAL_PAGE_READ_COUNT(void)
{
	return logical_page_read;
}
unsigned long int SSD_GET_PHYSICAL_PAGE_READ_COUNT(void)
{
	return physical_page_read;
}
unsigned long int SSD_GET_ERASE_COUNT(void)
{
	return erase_counter;
}

double SSD_UTIL_CALC(void)
{
	return (double)occupied_page_counter / PAGES_IN_SSD;
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



unsigned long SSD_TOTAL_WRITE_DELAY(void)
{
	return write_delay_sum;
}
unsigned long SSD_TOTAL_READ_DELAY(void)
{
	return read_delay_sum;
}


