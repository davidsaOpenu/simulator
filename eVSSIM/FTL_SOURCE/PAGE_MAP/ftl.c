// Copyright(c)2013 
//
// Hanyang University, Seoul, Korea
// Embedded Software Systems Lab. All right reserved

#include "common.h"
#include "qemu-kvm.h"

int g_init = 0;
extern double ssd_util;

void FTL_INIT(void)
{
	if(g_init == 0){
        	printf("[%s] start\n",__FUNCTION__);

		INIT_SSD_CONFIG();

		INIT_MAPPING_TABLE();
		INIT_INVERSE_PAGE_MAPPING();
		INIT_INVERSE_BLOCK_MAPPING();
		INIT_VALID_ARRAY();
		INIT_EMPTY_BLOCK_LIST();
		INIT_VICTIM_BLOCK_LIST();
		INIT_PERF_CHECKER();
		
		g_init = 1;

		SSD_IO_INIT();
		printf("[%s] complete\n",__FUNCTION__);
	}
}

void FTL_TERM(void)
{
	printf("[%s] start\n",__FUNCTION__);

	TERM_MAPPING_TABLE();
	TERM_INVERSE_PAGE_MAPPING();
	TERM_VALID_ARRAY();
	TERM_INVERSE_BLOCK_MAPPING();
	TERM_EMPTY_BLOCK_LIST();
	TERM_VICTIM_BLOCK_LIST();
	TERM_PERF_CHECKER();

	printf("[%s] complete\n",__FUNCTION__);
}

void FTL_READ(int32_t sector_nb, unsigned int length)
{
	int ret;

	ret = _FTL_READ(sector_nb, length);
}

void FTL_WRITE(int32_t sector_nb, unsigned int length)
{
	int ret;

	ret = _FTL_WRITE(sector_nb, length);
}

int _FTL_READ(int32_t sector_nb, unsigned int length)
{
#ifdef FTL_DEBUG
	printf("[%s] Start: sector_nb %d length %u\n",__FUNCTION__, sector_nb, length);
#endif

	if(sector_nb + length > SECTOR_NB){
		printf("Error[FTL_READ] Exceed Sector number\n"); 
		return FAIL;	
	}

	int32_t lpn;
	int32_t ppn;
	int32_t lba = sector_nb;
	unsigned int remain = length;
	unsigned int left_skip = sector_nb % SECTORS_PER_PAGE;
	unsigned int right_skip;
	unsigned int read_sects;

	unsigned int ret = FAIL;
	int read_page_nb = 0;
	int io_page_nb;

	while(remain > 0){

		if(remain > SECTORS_PER_PAGE - left_skip){
			right_skip = 0;
		}
		else{
			right_skip = SECTORS_PER_PAGE - left_skip - remain;
		}
		read_sects = SECTORS_PER_PAGE - left_skip - right_skip;

		lpn = lba / (int32_t)SECTORS_PER_PAGE;
		ppn = GET_MAPPING_INFO(lpn);

		if(ppn == -1){
			return FAIL;
		}

		lba += read_sects;
		remain -= read_sects;
		left_skip = 0;
	}

	io_alloc_overhead = ALLOC_IO_REQUEST(sector_nb, length, READ, &io_page_nb);

	remain = length;
	lba = sector_nb;
	left_skip = sector_nb % SECTORS_PER_PAGE;

	while(remain > 0){

		if(remain > SECTORS_PER_PAGE - left_skip){
			right_skip = 0;
		}
		else{
			right_skip = SECTORS_PER_PAGE - left_skip - remain;
		}
		read_sects = SECTORS_PER_PAGE - left_skip - right_skip;

		lpn = lba / (int32_t)SECTORS_PER_PAGE;

		ppn = GET_MAPPING_INFO(lpn);

		if(ppn == -1){
#ifdef FTL_DEBUG
			printf("Error[%s] No Mapping info\n",__FUNCTION__);
#endif
		}

		ret = SSD_PAGE_READ(CALC_FLASH(ppn), CALC_BLOCK(ppn), CALC_PAGE(ppn), read_page_nb, READ, io_page_nb);

#ifdef FTL_DEBUG
		if(ret == FAIL){
			printf("Error[%s] %u page read fail \n", __FUNCTION__, ppn);
		}
#endif
		read_page_nb++;

		lba += read_sects;
		remain -= read_sects;
		left_skip = 0;
	}

	INCREASE_IO_REQUEST_SEQ_NB();

#ifdef MONITOR_ON
	char szTemp[1024];
	sprintf(szTemp, "READ PAGE %d ", length);
	WRITE_LOG(szTemp);
#endif

#ifdef FTL_DEBUG
	printf("[%s] Complete\n",__FUNCTION__);
#endif

	return ret;
}

int _FTL_WRITE(int32_t sector_nb, unsigned int length)
{
#ifdef FTL_DEBUG
	printf("[%s] Start: sector_nb %d length %u\n",__FUNCTION__, sector_nb, length);
#endif
	int io_page_nb;

	if(sector_nb + length > SECTOR_NB){
		printf("Error[FTL_WRITE] Exceed Sector number\n");
                return FAIL;
        }
	else{
		io_alloc_overhead = ALLOC_IO_REQUEST(sector_nb, length, WRITE, &io_page_nb);
	}

	int32_t lba = sector_nb;
	int32_t lpn;
	int32_t new_ppn;

	unsigned int remain = length;
	unsigned int left_skip = sector_nb % SECTORS_PER_PAGE;
	unsigned int right_skip;
	unsigned int write_sects;

	unsigned int ret = FAIL;
	int write_page_nb=0;

	while(remain > 0){

		if(remain > SECTORS_PER_PAGE - left_skip){
			right_skip = 0;
		}
		else{
			right_skip = SECTORS_PER_PAGE - left_skip - remain;
		}

		write_sects = SECTORS_PER_PAGE - left_skip - right_skip;

		ret = GET_NEW_PAGE(VICTIM_OVERALL, EMPTY_TABLE_ENTRY_NB, &new_ppn);
		if(ret == FAIL){
			printf("ERROR[FTL_WRITE] Get new page fail \n");
			return FAIL;
		}

		ret = SSD_PAGE_WRITE(CALC_FLASH(new_ppn), CALC_BLOCK(new_ppn), CALC_PAGE(new_ppn), write_page_nb, WRITE, io_page_nb);

		write_page_nb++;

		lpn = lba / (int32_t)SECTORS_PER_PAGE;
		UPDATE_OLD_PAGE_MAPPING(lpn);
		UPDATE_NEW_PAGE_MAPPING(lpn, new_ppn);

#ifdef FTL_DEBUG
                if(ret == FAIL){
                        printf("Error[FTL_WRITE] %d page write fail \n", new_ppn);
                }
#endif
		lba += write_sects;
		remain -= write_sects;
		left_skip = 0;
	}


	INCREASE_IO_REQUEST_SEQ_NB();

#ifdef GC_ON
	GC_CHECK(CALC_FLASH(new_ppn), CALC_BLOCK(new_ppn));
#endif

#ifdef MONITOR_ON
	char szTemp[1024];
	sprintf(szTemp, "WRITE PAGE %d ", length);
	WRITE_LOG(szTemp);
	sprintf(szTemp, "WB CORRECT %d", write_page_nb);
	WRITE_LOG(szTemp);
#endif

#ifdef FTL_DEBUG
	printf("[%s] Complete\n",__FUNCTION__);
#endif
	return ret;
}
