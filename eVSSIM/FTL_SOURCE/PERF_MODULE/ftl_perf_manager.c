// Copyright(c)2013 
//
// Hanyang University, Seoul, Korea
// Embedded Software Systems Lab. All right reserved

#include "common.h"

/* Average IO Time */
double avg_write_delay;
double total_write_count;
double total_write_delay;

double avg_read_delay;
double total_read_count;
double total_read_delay;

double avg_gc_write_delay;
double total_gc_write_count;
double total_gc_write_delay;

double avg_gc_read_delay;
double total_gc_read_count;
double total_gc_read_delay;

/* IO Latency */
unsigned int io_request_nb;
unsigned int io_request_seq_nb;

struct io_request* io_request_start;
struct io_request* io_request_end;

/* Calculate IO Latency */
double read_latency_count;
double write_latency_count;

double avg_read_latency;
double avg_write_latency;

/* SSD Util */
double ssd_util;
int64_t written_page_nb;

void INIT_PERF_CHECKER(void){

	/* Average IO Time */
	avg_write_delay = 0;
	total_write_count = 0;
	total_write_delay = 0;

	avg_read_delay = 0;
	total_read_count = 0;
	total_read_delay = 0;

	avg_gc_write_delay = 0;
	total_gc_write_count = 0;
	total_gc_write_delay = 0;

	avg_gc_read_delay = 0;
	total_gc_read_count = 0;
	total_gc_read_delay = 0;

	/* IO Latency */
	io_request_nb = 0;
	io_request_seq_nb = 0;

	io_request_start = NULL;
	io_request_end = NULL;

	read_latency_count = 0;
	write_latency_count = 0;

	avg_read_latency = 0;
	avg_write_latency = 0;

	ssd_util = 0;
	written_page_nb = 0;
}

void TERM_PERF_CHECKER(void){

	while (io_request_nb) {
		FREE_IO_REQUEST(io_request_start);
	}

	printf("Average Read Latency	%.3lf us\n", avg_read_latency);
	printf("Average Write Latency	%.3lf us\n", avg_write_latency);
}

void SEND_TO_PERF_CHECKER(int op_type, int64_t op_delay, int type){

	double delay = (double)op_delay;
	if(type == CH_OP){
		switch(op_type){
			case READ:
				total_read_delay += delay;
				total_read_count++;
				avg_read_delay = total_read_delay / total_read_count;
				break;

			case WRITE:
				total_write_delay += delay;
				break;

			case ERASE:
				break;

			case GC_READ:
				total_gc_read_delay += delay;
				break;

			case GC_WRITE:
				total_gc_write_delay += delay;
				break;
			case COPYBACK:
				break;
			default:
				break;
		}
	}
	else if(type == REG_OP){
		switch (op_type){
			case READ:
				total_read_delay += delay;
				break;

			case WRITE:
				total_write_delay += delay;
				total_write_count++;
				avg_write_delay = total_write_delay / total_write_count;

				/* Calc SSD Util */
				written_page_nb++;
				break;

			case ERASE:
				written_page_nb -= PAGE_NB;
				break;

			case GC_READ:
				total_gc_read_delay += delay;
				total_gc_read_count++;
				avg_gc_read_delay = total_gc_read_delay / total_gc_read_count;
				break;

			case GC_WRITE:
				total_gc_write_delay += delay;
				total_gc_write_count++;
				avg_gc_write_delay = total_gc_write_delay / total_gc_write_count;

				/* Calc SSD Util */
				written_page_nb++;
				break;
			case COPYBACK:
				break;
			default:
				break;
		}

		ssd_util = (double)((double)written_page_nb / PAGES_IN_SSD)*100;
	}
	else if(type == LATENCY_OP){
		switch (op_type){
			case READ:
				avg_read_latency = (avg_read_latency * read_latency_count + delay)/(read_latency_count + 1);

				read_latency_count++;
				break;
			case WRITE:
				avg_write_latency = (avg_write_latency * write_latency_count + delay)/(write_latency_count + 1);
				write_latency_count++;
				break;
			default:
				break;
		}
	}
}

double GET_IO_BANDWIDTH(double delay)
{
	double bw;

	if(delay != 0)
		bw = ((double)GET_PAGE_SIZE()*SECOND_IN_USEC)/(delay*MEGABYTE_IN_BYTES);
	else
		bw = 0;

	return bw;

}

int64_t ALLOC_IO_REQUEST(uint32_t sector_nb, unsigned int length, int io_type, int* page_nb)
{
	int64_t start = get_usec();
	int io_page_nb = 0;
	unsigned int remain = length;
	unsigned int left_skip = sector_nb % SECTORS_PER_PAGE;
	unsigned int right_skip;
	unsigned int sects;

	io_request* curr_io_request = (io_request*)calloc(1, sizeof(io_request));
	if (curr_io_request == NULL)
		RERR(0, "Calloc io_request fail\n");

	while(remain > 0){
		if(remain > SECTORS_PER_PAGE - left_skip){
			right_skip = 0;
		}
		else{
			right_skip = SECTORS_PER_PAGE - left_skip - remain;
		}
		sects = SECTORS_PER_PAGE - left_skip - right_skip;

		remain -= sects;
		left_skip = 0;
		io_page_nb++;
	}

	*page_nb = io_page_nb;
	int64_t* start_time_arr = (int64_t*)calloc(io_page_nb, sizeof(int64_t));
	int64_t* end_time_arr = (int64_t*)calloc(io_page_nb, sizeof(int64_t));

	if (start_time_arr == NULL || end_time_arr == NULL)
		RERR(0, "Calloc time array fail\n");
	memset(start_time_arr, 0, io_page_nb);
	memset(end_time_arr, 0, io_page_nb);

	curr_io_request->request_nb = io_request_seq_nb;
	
	curr_io_request->request_type = io_type;
	curr_io_request->request_size = io_page_nb;
	curr_io_request->start_count = 0;
	curr_io_request->end_count = 0;
	curr_io_request->start_time = start_time_arr;
	curr_io_request->end_time = end_time_arr;
	curr_io_request->next = NULL;

	if(io_request_start == NULL && io_request_nb == 0){
		io_request_start = curr_io_request;
		io_request_end = curr_io_request;
	}
	else{
		io_request_end->next = curr_io_request;
		io_request_end = curr_io_request;
	}
	io_request_nb++;
	
	int64_t end = get_usec();

	return (end - start);
}

void FREE_DUMMY_IO_REQUEST(int type)
{
	int i;
	int success = 0;
	io_request* prev_request = io_request_start;

	io_request* request = LOOKUP_IO_REQUEST(io_request_seq_nb, type);


	if(io_request_nb == 1){
		io_request_start = NULL;
		io_request_end = NULL;
		success = 1;
	}
	else if(prev_request == request){
		io_request_start = request->next;
		success = 1;
	}
	else{
		for(i=0;i<(io_request_nb-1);i++){
			if(prev_request->next == request && request == io_request_end){
				prev_request->next = NULL;
				io_request_end = prev_request;
				success = 1;
				break;
			}
			else if(prev_request->next == request){
				prev_request->next = request->next;
				success = 1;
				break;
			}
			else{
				prev_request = prev_request->next;
			}
		}
	}

	if (success == 0)
		RERR(, "There is no such io request\n");

	free(request->start_time);
	free(request->end_time);
	free(request);

	io_request_nb--;
}

void FREE_IO_REQUEST(io_request* request)
{
	int i;
	int success = 0;
	io_request* prev_request = io_request_start;

	if(io_request_nb == 1){
		io_request_start = NULL;
		io_request_end = NULL;
		success = 1;
	}
	else if(prev_request == request){
		io_request_start = request->next;
		success = 1;
	}
	else{
		for(i=0;i<(io_request_nb-1);i++){
			if(prev_request->next == request && request == io_request_end){
				prev_request->next = NULL;
				io_request_end = prev_request;
				success = 1;
				break;
			}
			else if(prev_request->next == request){
				prev_request->next = request->next;
				success = 1;
				break;
			}
			else{
				prev_request = prev_request->next;
			}
		}
	}

	if (success == 0)
		RERR(, "There is no such io request\n");

	free(request->start_time);
	free(request->end_time);
	free(request);

	io_request_nb--;
}

int64_t UPDATE_IO_REQUEST(int request_nb, int offset, int64_t time, int type)
{
	int64_t start = get_usec();

	int io_type;
	int64_t latency=0;
	int flag = 0;

	if(request_nb == -1)
		return 0;

	io_request* curr_request = LOOKUP_IO_REQUEST(request_nb, type);
	if (curr_request == NULL)
		RDBG_FTL(0, "No such io request, nb %d\n", request_nb);

	if(type == UPDATE_START_TIME){
		curr_request->start_time[offset] = time;
		curr_request->start_count++;	
	}
	else if(type == UPDATE_END_TIME){
		curr_request->end_time[offset] = time;
		curr_request->end_count++;
	}

	if(curr_request->start_count == curr_request->request_size && curr_request->end_count == curr_request->request_size){
		latency = CALC_IO_LATENCY(curr_request);
		io_type = curr_request->request_type;

		SEND_TO_PERF_CHECKER(io_type, latency, LATENCY_OP);

		FREE_IO_REQUEST(curr_request);
		flag = 1;
	}
	int64_t end = get_usec();

	return (end - start);
}

void INCREASE_IO_REQUEST_SEQ_NB(void)
{
	if(io_request_seq_nb == 0xffffffff){
		io_request_seq_nb = 0;
	}
	else{
		io_request_seq_nb++;
	}
}

io_request* LOOKUP_IO_REQUEST(int request_nb, int type)
{
	int i;
	int total_request=0;
	io_request* curr_request = NULL;

	if(io_request_start != NULL){
		curr_request = io_request_start;
		total_request = io_request_nb;
	}
	else
		RDBG_FTL(NULL, "There is no request\n");

	for(i=0;i<total_request;i++){
		if(curr_request->request_nb == request_nb){
			return curr_request;
		}

		if(curr_request->next != NULL){
			curr_request = curr_request->next;
		}
		else{
			return NULL;
		}	
	}

	return NULL;
}

int64_t CALC_IO_LATENCY(io_request* request)
{
	int64_t latency;
	int64_t* start_time_arr = request->start_time;
	int64_t* end_time_arr = request->end_time;

	int64_t min_start_time;
	int64_t max_end_time;

	int i;
	int type = request->request_type;
	int size = request->request_size;

	for(i=0; i<size; i++){
		if(end_time_arr[i] == 0){
			if(type == READ){
				end_time_arr[i] = start_time_arr[i] + REG_READ_DELAY + CELL_READ_DELAY;
			}
			else if(type == WRITE){
				end_time_arr[i] = start_time_arr[i] + REG_WRITE_DELAY + CELL_PROGRAM_DELAY;
			}
		}
	}

	min_start_time = start_time_arr[0];
	max_end_time = end_time_arr[0];

	if(size > 1){
		for(i=1; i<size; i++){
			if(min_start_time > start_time_arr[i]){
				min_start_time = start_time_arr[i];
			}
			if(max_end_time < end_time_arr[i]){
				max_end_time = end_time_arr[i];
			}
		}
	}
	
	latency = (max_end_time - min_start_time)/(request->request_size);
	
	return latency;
}

