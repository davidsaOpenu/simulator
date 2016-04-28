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

void SSD_WRITE(unsigned int length, uint32_t sector_nb)
{
	FTL_WRITE((uint64_t)sector_nb, 0, length);
}

void SSD_READ(unsigned int length, uint32_t sector_nb)
{
	FTL_READ((uint64_t)sector_nb, 0, length);
}
