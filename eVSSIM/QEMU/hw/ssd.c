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

void SSD_WRITE(int32_t id, unsigned int offset, unsigned int length)
{
	FTL_WRITE(id, offset, length);
}

void SSD_READ(int32_t id, unsigned int offset, unsigned int length)
{
	FTL_READ(id, offset, length);
}

