// Copyright(c)2013 
//
// Hanyang University, Seoul, Korea
// Embedded Software Systems Lab. All right reserved

#ifndef _LOG_MONITOR_H_
#define _LOG_MONITOR_H_

#include <stdio.h>		/* scanf */
#include <stdlib.h>     /* atoi */

typedef struct {
	int write_count;
	double write_speed;
	int write_sector_count;

	int read_count;
	double read_speed;
	int read_sector_count;

	int gc_count;
	int gc_exchange;
	int gc_sector;

	int erase_count;

	int trim_count;
	int trim_effect;
	int written_page;
	double write_amplification;
	double utils;
} logger_monitor;

void PARSE_LOG(const char* log_line, logger_monitor *monitor);
void INIT_LOGGER_MONITOR(logger_monitor *monitor);

#endif
