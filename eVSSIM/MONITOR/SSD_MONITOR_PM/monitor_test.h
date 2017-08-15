#ifndef __MONITOR_MAIN_H__
#define __MONITOR_MAIN_H__

#include <pthread.h>

#include "../../LOG_MGR/logging_rt_analyzer.h"


extern pthread_mutex_t win_lock;

void* start_monitor(void* args);


void update_stats(SSDStatistics stats);


#endif
