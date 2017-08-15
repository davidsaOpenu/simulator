#ifndef __MONITOR_MAIN_H__
#define __MONITOR_MAIN_H__

#include "../../LOG_MGR/logging_rt_analyzer.h"


void* start_monitor(void* args);


void update_stats(SSDStatistics stats);


#endif
