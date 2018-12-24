// Copyright(c)2013 
//
// Hanyang University, Seoul, Korea
// Embedded Software Systems Lab. All right reserved

#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <memory.h>
#include <errno.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdarg.h>
#include <signal.h>

#include "ssd_log_monitor.h"

int _next_word(char* destination, const char* source, int start) {

	int l = strlen(source);
	if( start >= l ) return -1;

	int i;
	for(i = 0; i < l - start; i++) {
		if( source[i + start] == ' ' ) {
			break;
		} else {
			destination[i] = source[i + start];
		}
	}

	destination[i] = '\0';
	return i + start;
}

void PARSE_LOG(const char* log_line, logger_monitor *monitor) {

	char next_word[200];

	int pp = _next_word(next_word, log_line, 0);
	if( pp >= 0 ) {

		if( strcmp(next_word, "WRITE") == 0 )  {
	    	pp = _next_word(next_word, log_line, pp + 1);

			if( strcmp(next_word, "PAGE") == 0 ) {
				pp = _next_word(next_word, log_line, pp + 1);
				unsigned int length = atoi(next_word);
				monitor->write_count += length;

			} else if( strcmp(next_word, "SECTOR") == 0 ) {
				pp = _next_word(next_word, log_line, pp + 1);
				unsigned int length = atoi(next_word);
				monitor->write_sector_count += length;

			} else if( strcmp(next_word, "BW") == 0 ) {

		    	pp = _next_word(next_word, log_line, pp + 1);
				sscanf(next_word, "%lf", &(monitor->write_speed));
			}
		} else if( strcmp(next_word, "READ") == 0 ) {

	    	pp = _next_word(next_word, log_line, pp + 1);
			if( strcmp(next_word, "PAGE") == 0 ) {
				pp = _next_word(next_word, log_line, pp + 1);
				unsigned int length = atoi(next_word);
				monitor->read_count += length;

			} else if( strcmp(next_word, "SECTOR") == 0 ) {
				pp = _next_word(next_word, log_line, pp + 1);
				unsigned int length = atoi(next_word);
				monitor->read_sector_count += length;

			} else if( strcmp(next_word, "BW") == 0 ) {
		    	pp = _next_word(next_word, log_line, pp + 1);
				sscanf(next_word, "%lf", &(monitor->read_speed));
			}
		} else if( strcmp(next_word, "GC") == 0 ) {

			monitor->gc_count++;

		} else if( strcmp(next_word, "ERASE") == 0 ) {

			monitor->erase_count++;

		} else if( strcmp(next_word, "WB") == 0 ) {

	    	pp = _next_word(next_word, log_line, pp + 1);

	    	if( strcmp(next_word, "CORRECT") == 0 ) {

		    	pp = _next_word(next_word, log_line, pp + 1);
				monitor->written_pages += atoi(next_word);

			} else {
		    	pp = _next_word(next_word, log_line, pp + 1);
				sscanf(next_word, "%lf", &(monitor->write_amplification));
			}

		} else if( strcmp(next_word, "TRIM") == 0 ) {

	    	pp = _next_word(next_word, log_line, pp + 1);
			if( strcmp(next_word, "INSERT") == 0 ) {
				monitor->trim_count++;
			} else {

		    	pp = _next_word(next_word, log_line, pp + 1);
				int effect = atoi(next_word);
				monitor->trim_effect += effect;
			}
		} else if( strcmp(next_word, "UTIL") == 0 ) {
	    	pp = _next_word(next_word, log_line, pp + 1);
			sscanf(next_word, "%lf", &(monitor->utils));
		}
	}

}

void INIT_LOGGER_MONITOR(logger_monitor *monitor) {

	monitor->write_count = 0;
	monitor->write_speed = 0.0;
	monitor->write_sector_count = 0;

	monitor->read_count = 0;
	monitor->read_speed = 0.0;
	monitor->read_sector_count = 0;

	monitor->gc_count = 0;
	monitor->gc_exchange = 0;
	monitor->gc_sector = 0;

	monitor->erase_count = 0;

	monitor->trim_count = 0;
	monitor->trim_effect = 0;
	monitor->written_pages = 0;

	monitor->write_amplification = 0.0;
	monitor->utils = 0.0;
}
