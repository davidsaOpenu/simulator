#include "ssd.h"
#include "common.h"

#ifdef GET_WORKLOAD
#include <time.h>
#include <sys/time.h>
#endif

void SSD_INIT(void)
{
	FTL_INIT();
#ifdef MONITOR_ON
	INIT_LOG_MANAGER();
#endif
}

void SSD_TERM(void)
{	
	FTL_TERM();

#ifdef MONITOR_ON
	TERM_LOG_MANAGER();
#endif
}

void SSD_WRITE(unsigned int length, int32_t sector_nb)
{
	FTL_WRITE(sector_nb, length);
}

void SSD_READ(unsigned int length, int32_t sector_nb)
{
	FTL_READ(sector_nb, length);
}

