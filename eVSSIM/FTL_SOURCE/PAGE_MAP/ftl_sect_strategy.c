#include "common.h"
#include "ftl_sect_strategy.h"

extern ssd_disk ssd;

ftl_ret_val _FTL_READ_SECT(uint32_t nsid, uint64_t sector_nb, unsigned int length)
{
	PDBG_FTL("Start: sector_nb %ld length %u\n", sector_nb, length);

	const uint64_t NUM_SECTORS_IN_NS = (uint64_t)NAMESPACES_SIZE[nsid] * (uint64_t)SECTORS_PER_PAGE * (uint64_t)PAGE_NB;

	if (sector_nb + length > NUM_SECTORS_IN_NS)
		RERR(FTL_FAILURE, "[FTL_READ] Exceed Sector number\n");

	uint64_t lpn;
	uint64_t ppn;
	uint64_t lba = sector_nb;
	unsigned int remain = length;
	unsigned long left_skip = sector_nb % SECTORS_PER_PAGE;
	unsigned long right_skip;
	unsigned int read_sects;

	unsigned int ret = FTL_FAILURE;
	int read_page_nb = 0;
	int io_page_nb;

	// just calculate the overhead of allocating the request. io_page_nb will be the total number of pages we're gonna read
	io_alloc_overhead = ALLOC_IO_REQUEST(sector_nb, length, READ, &io_page_nb);

	remain = length;
	lba = sector_nb;
	left_skip = sector_nb % SECTORS_PER_PAGE;

	while (remain > 0)
	{
		if (remain > SECTORS_PER_PAGE - left_skip)
		{
			right_skip = 0;
		}
		else
		{
			right_skip = SECTORS_PER_PAGE - left_skip - remain;
		}

		read_sects = SECTORS_PER_PAGE - left_skip - right_skip;

		lpn = lba / (int32_t)SECTORS_PER_PAGE;

		//Send a logical read action being done to the statistics gathering
		FTL_STATISTICS_GATHERING(lpn , LOGICAL_READ);

		ppn = GET_MAPPING_INFO(nsid, lpn);

		if (ppn == (uint64_t)-1)
			RDBG_FTL(FTL_FAILURE, "No Mapping info\n");

		ret = SSD_PAGE_READ(CALC_FLASH(ppn), CALC_BLOCK(ppn), CALC_PAGE(ppn), read_page_nb, READ);

		//Send a physical read action being done to the statistics gathering
		if (ret == FTL_SUCCESS)
		{
			FTL_STATISTICS_GATHERING(ppn , PHYSICAL_READ);
		}

#ifdef FTL_DEBUG
		if (ret == FTL_FAILURE)
			PERR("%lu page read fail \n", ppn);
#endif
		read_page_nb++;

		lba += read_sects;
		remain -= read_sects;
		left_skip = 0;

		// Normally, there would be a LOG_LOGICAL_CELL_READ call here. As it happens, each physical read
		// corresponds exactly to one logical read with the exact same parameters; thus, we omit the logical
		// read log all together, and refer to the physical one as an indication to both.
	}

	INCREASE_IO_REQUEST_SEQ_NB();

	PDBG_FTL("Complete\n");

	return ret;
}

ftl_ret_val _FTL_WRITE(uint32_t nsid, uint64_t sector_nb, unsigned int length)
{
    return _FTL_WRITE_SECT(nsid, sector_nb, length);
}

ftl_ret_val _FTL_WRITE_SECT(uint32_t nsid, uint64_t sector_nb, unsigned int length)
{
	PDBG_FTL("Start: sector_nb %" PRIu64 " length %u\n", sector_nb, length);

	int io_page_nb;

	const uint64_t NUM_SECTORS_IN_NS = (uint64_t)NAMESPACES_SIZE[nsid] * (uint64_t)SECTORS_PER_PAGE * (uint64_t)PAGE_NB;

	if (sector_nb + length > NUM_SECTORS_IN_NS)
		RERR(FTL_FAILURE, "Exceed Sector number\n");

	io_alloc_overhead = ALLOC_IO_REQUEST(sector_nb, length, WRITE, &io_page_nb);

	uint64_t lba = sector_nb;
	uint64_t lpn;
	uint64_t new_ppn;

	unsigned int remain = length;
	unsigned int left_skip = sector_nb % SECTORS_PER_PAGE;
	unsigned int right_skip;
	unsigned int write_sects;

	unsigned int ret = FTL_FAILURE;
	int write_page_nb=0;

	while (remain > 0)
	{
		if (remain > SECTORS_PER_PAGE - left_skip)
		{
			right_skip = 0;
		}
		else
		{
			right_skip = SECTORS_PER_PAGE - left_skip - remain;
		}

		write_sects = SECTORS_PER_PAGE - left_skip - right_skip;

		ret = GET_NEW_PAGE(VICTIM_OVERALL, EMPTY_TABLE_ENTRY_NB, &new_ppn);
		if (ret == FTL_FAILURE)
			RERR(FTL_FAILURE, "[FTL_WRITE] Get new page fail \n");

		ret = SSD_PAGE_WRITE(CALC_FLASH(new_ppn), CALC_BLOCK(new_ppn), CALC_PAGE(new_ppn), write_page_nb, WRITE);

		//we caused a block write -> update the logical block_write counter + update the physical block write counter
		wa_counters.logical_block_write_counter++;
		wa_counters.physical_block_write_counter++;

		//Send a physical write action being done to the statistics gathering
		if (ret == FTL_SUCCESS)
		{
			FTL_STATISTICS_GATHERING(new_ppn , PHYSICAL_WRITE);
		}
		else if (ret == FTL_FAILURE)
		{
			// Prints error only as debug. 
			PDBG_FTL("Error[FTL_WRITE] %lu page write fail \n", new_ppn)
		}

		write_page_nb++;

		//Calculate the logical page number -> the current sector_number / amount_of_sectors_per_page
		lpn = lba / (uint64_t)SECTORS_PER_PAGE;

		//Send a logical write action being done to the statistics gathering
		FTL_STATISTICS_GATHERING(lpn , LOGICAL_WRITE);

		// logical page number to physical. will need to be changed to account for objectid
		UPDATE_OLD_PAGE_MAPPING(nsid, lpn);
		UPDATE_NEW_PAGE_MAPPING(nsid, lpn, new_ppn);

		lba += write_sects;
		remain -= write_sects;
		left_skip = 0;
	}

	INCREASE_IO_REQUEST_SEQ_NB();

#ifdef GC_ON
	GC_CHECK(CALC_FLASH(new_ppn), CALC_BLOCK(new_ppn), false, false); // is this a bug? gc will only happen on the last page's flash and block
#endif

    PDBG_FTL("Complete\n");

	return ret;
}

//Get 2 physical page address, the source page which need to be moved to the destination page
ftl_ret_val _FTL_COPYBACK(uint64_t source, uint64_t destination)
{
	uint32_t nsid;
	uint64_t lpn; //The logical page address, the page that being moved.
	unsigned int ret = FTL_FAILURE;

	//Handle copyback delays
	ret = SSD_PAGE_COPYBACK(source, destination, COPYBACK);

    // actual page swap, go korea
    /*SSD_PAGE_READ(CALC_FLASH(source), CALC_BLOCK(source), CALC_PAGE(source), 0, GC_READ);
    SSD_PAGE_WRITE(CALC_FLASH(destination), CALC_BLOCK(destination), CALC_PAGE(destination), 0, GC_WRITE);
    lpn = GET_INVERSE_MAPPING_INFO(source);
    UPDATE_NEW_PAGE_MAPPING(lpn, destination);*/


	if (ret == FTL_FAILURE)
        RDBG_FTL(FTL_FAILURE, "%lu page copyback fail \n", source);

	//Handle page map
	GET_INVERSE_MAPPING_INFO(source, &nsid, &lpn);
	if (lpn != (uint64_t)-1)
	{
		// The given physical page is being map, the mapping information need to be changed,
		UPDATE_OLD_PAGE_MAPPING(nsid, lpn); //as far as I can tell when being called under the gc manage all the actions are being done, but what if will be called from another place?
		UPDATE_NEW_PAGE_MAPPING(nsid, lpn, destination);
	}

	return ret;
}

ftl_ret_val _FTL_CREATE(void)
{
    // no "creation" in address-based storage
    return FTL_SUCCESS;
}

ftl_ret_val _FTL_DELETE(void)
{
    // no "deletion" in address-based storage
    return FTL_SUCCESS;
}
