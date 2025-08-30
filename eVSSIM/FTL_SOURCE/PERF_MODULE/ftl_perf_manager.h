// Copyright(c)2013
//
// Hanyang University, Seoul, Korea
// Embedded Software Systems Lab. All right reserved

#ifndef _PERF_MANAGER_H_
#define _PERF_MANAGER_H_

#include <stdint.h>

#define MEGABYTE_IN_BYTES (1024*1024)
#define SECOND_IN_USEC 1000000
#define CALCULATEMBPS(s,t) ((double)s*SECOND_IN_USEC)/MEGABYTE_IN_BYTES/t;

#define ERROR_THRESHHOLD(x) x*0.01

/* IO Latency */
typedef struct io_request
{
	unsigned int request_nb;
	int	request_type	: 16;
	int	request_size	: 16;
	int	start_count	: 16;
	int	end_count	: 16;
	int64_t* 	start_time;
	int64_t* 	end_time;
	struct io_request* next;
}io_request;

/* IO Latency */
extern unsigned int io_request_nb;
extern unsigned int io_request_seq_nb;

extern struct io_request* io_request_start;
extern struct io_request* io_request_end;

/* GC Latency */
extern unsigned int gc_request_nb;
extern unsigned int gc_request_seq_nb;

extern struct io_request* gc_request_start;
extern struct io_request* gc_request_end;

double GET_IO_BANDWIDTH(uint8_t device_index, double delay);

void INIT_PERF_CHECKER(void);
void TERM_PERF_CHECKER(void);

void SEND_TO_PERF_CHECKER(uint8_t device_index, int op_type, int64_t op_delay, int type);

int64_t ALLOC_IO_REQUEST(uint8_t device_index, uint32_t sector_nb, unsigned int length, int io_type, int* page_nb);
void FREE_DUMMY_IO_REQUEST(void);
void FREE_IO_REQUEST(io_request* request);
int64_t UPDATE_IO_REQUEST(uint8_t device_index, uint32_t request_nb, int offset, int64_t time, int type);
void INCREASE_IO_REQUEST_SEQ_NB(void);
io_request* LOOKUP_IO_REQUEST(uint32_t request_nb);
int64_t CALC_IO_LATENCY(uint8_t device_index, io_request* request);

#endif
