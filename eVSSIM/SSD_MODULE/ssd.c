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

int SSD_CREATE(unsigned int length)
{
	return FTL_CREATE(length);
}

void SSD_OBJECT_READ(object_location obj_loc, unsigned int length)
{
	_FTL_OBJ_READ(obj_loc.object_id, 0, length);
}

void SSD_OBJECT_WRITE(object_location obj_loc, unsigned int length)
{
	unsigned int id = _FTL_OBJ_CREATE(length);

	if (id)
	{
		int res = _FTL_OBJ_WRITE(id, 0, length);
		printf("NIR-->created obj res: %d\n", res);
	}
}

//This should probably move to a standalone header file
void OSD_WRITE(object_location obj_loc, unsigned int length, uint64_t addr)
{
	return;
}

//This should probably move to a standalone header file
void OSD_READ(object_location obj_loc, unsigned int length, uint64_t addr)
{
	return;
}
