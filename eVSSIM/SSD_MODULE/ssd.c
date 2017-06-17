#include "ssd.h"
#include "common.h"

#ifdef GET_WORKLOAD
#include <time.h>
#include <sys/time.h>
#endif

void SSD_INIT(void)
{
	FTL_INIT();
	INIT_LOG_MANAGER();
}

void SSD_TERM(void)
{	
	FTL_TERM();
	TERM_LOG_MANAGER();
}

void SSD_WRITE(unsigned int length, uint32_t sector_nb)
{
	_FTL_WRITE((uint64_t)sector_nb, 0, length);
}

void SSD_READ(unsigned int length, uint32_t sector_nb)
{
	_FTL_READ((uint64_t)sector_nb, 0, length);
}

