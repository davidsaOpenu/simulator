#include "common.h"
#include "ftl_sect_strategy.h"
#include "ssd_file_operations.h"

extern ssd_disk ssd;

static uint64_t physical_address_from_logical_address(uint64_t lba, uint64_t* o_ppn);

static ftl_ret_val _READ_STATUS_ENHANCED(void);

static uint64_t physical_address_from_logical_address(uint64_t lba, uint64_t* o_ppn) {
	uint64_t lpn = lba / (int32_t)SECTORS_PER_PAGE;
	uint64_t offset_in_page = lba % (int32_t)SECTORS_PER_PAGE;
	uint64_t ppn = GET_MAPPING_INFO(lpn);
	if (o_ppn != NULL) {
		*o_ppn = ppn;
	}
	if (ppn == (uint64_t)-1) return -1;
	return ppn * GET_PAGE_SIZE() + offset_in_page;
}

ftl_ret_val _FTL_READ(uint64_t sector_nb, unsigned int length, unsigned char *data)
{
	return _FTL_READ_SECT(sector_nb, length, data);
}

ftl_ret_val _FTL_READ_SECT(uint64_t sector_nb, unsigned int length, unsigned char *data)
{

	PDBG_FTL("Start: sector_nb %ld length %u\n", sector_nb, length);

	if (sector_nb + length > SECTOR_NB)
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

		ppn = GET_MAPPING_INFO(lpn);

		if (ppn == (uint64_t)-1) {
			RDBG_FTL(FTL_FAILURE, "No Mapping info\n");
		}

		ret = SSD_PAGE_READ(CALC_FLASH(ppn), CALC_BLOCK(ppn), CALC_PAGE(ppn), read_page_nb, READ);
		//Send a physical read action being done to the statistics gathering
		if (ret == FTL_SUCCESS)
		{
			FTL_STATISTICS_GATHERING(ppn , PHYSICAL_READ);
			size_t abs_physical_offset = ppn * GET_PAGE_SIZE() + lba % SECTORS_PER_PAGE;
			if (data != NULL && 
				ssd_read(GET_FILE_NAME(), abs_physical_offset, read_sects * GET_SECTOR_SIZE(), data) != SSD_FILE_OPS_SUCCESS) {
				RDBG_FTL(FTL_FAILURE, "Failed to read from ppn %lu\n", ppn);
			}
		}

#ifdef FTL_DEBUG
		if (ret == FTL_FAILURE)
			PERR("%u page read fail \n", ppn);
#endif
		read_page_nb++;

		lba += read_sects;
		remain -= read_sects;
		left_skip = 0;
		data += read_sects * GET_SECTOR_SIZE();

		// Normally, there would be a LOG_LOGICAL_CELL_READ call here. As it happens, each physical read
		// corresponds exactly to one logical read with the exact same parameters; thus, we omit the logical
		// read log all together, and refer to the physical one as an indication to both.
	}

	INCREASE_IO_REQUEST_SEQ_NB();

	PDBG_FTL("Complete\n");

	return ret;
}

ftl_ret_val _FTL_WRITE(uint64_t sector_nb, unsigned int length, const unsigned char *data)
{
    return _FTL_WRITE_SECT(sector_nb, length, data);
}

// **
// * The following functions are internal helper functions for the use of _FTL_WRITE_SECT
// **
static ftl_ret_val read_status_enanched = FTL_FAILURE;

// Checks if a writing is program compatible (there is not actuall writing here).
// NOTE: We assume the parameters are for writing in a single page amount of `length` sectors. 
static void _FTL_WRITE_DRY_SECT(uint64_t lba, unsigned int length, const unsigned char *data) {
	read_status_enanched = FTL_FAILURE;
	if (data == NULL) {
		read_status_enanched = FTL_FAILURE;
		return;
	}

	size_t abs_physical_offset =  physical_address_from_logical_address(lba, NULL);
	if (abs_physical_offset == (uint64_t)-1) {
		read_status_enanched = FTL_FAILURE;
		return;
	}

	if (is_program_compatible(GET_FILE_NAME(), abs_physical_offset, length * GET_SECTOR_SIZE(), data)) {
		read_status_enanched = FTL_SUCCESS;
		return;
	}
}

// Returns the result of the last call to _FTL_WRITE_DRY_SECT
static ftl_ret_val _READ_STATUS_ENHANCED(void) {
	return read_status_enanched;
}

// Writes to the ssd without erasing the current mapped physical page.
// NOTE: We assume the parameters are for writing in a single page amount of `length` sectors. 
//		 And we assume the writing happens after making sure the writing is program compatible.
static ftl_ret_val _FTL_WRITE_COMMIT(uint64_t lba, int write_page_nb, unsigned int length, const unsigned char *data) {
	if (data == NULL) {
		return FTL_FAILURE;
	}

	uint64_t ppn = -1;
	size_t abs_physical_offset = physical_address_from_logical_address(lba, &ppn);
	if (abs_physical_offset == (uint64_t)-1 || ppn == (uint64_t)-1) {
		return FTL_FAILURE;
	}
	ftl_ret_val ret = SSD_PAGE_WRITE(CALC_FLASH(ppn), CALC_BLOCK(ppn), CALC_PAGE(ppn), write_page_nb, WRITE_COMMIT);
	if (ret == FTL_SUCCESS && ssd_write(GET_FILE_NAME(), abs_physical_offset, length * GET_SECTOR_SIZE(), data) == SSD_FILE_OPS_SUCCESS) {
		return FTL_SUCCESS;
	}

	return FTL_FAILURE;
}
// **
// * End of helper functions for the use of _FTL_WRITE_SECT
// **

ftl_ret_val _FTL_WRITE_SECT(uint64_t sector_nb, unsigned int length, const unsigned char *data)
{
	(void)data;
	PDBG_FTL("Start: sector_nb %" PRIu64 "length %u\n", sector_nb, length);

	int io_page_nb;

	if (sector_nb + length > SECTOR_NB)
		RERR(FTL_FAILURE, "Exceed Sector number\n");
	io_alloc_overhead = ALLOC_IO_REQUEST(sector_nb, length, WRITE, &io_page_nb);

	uint64_t lba = sector_nb; // logical block address
	uint64_t lpn;			  // logical page number
	uint64_t new_ppn = (uint64_t)-1; // physical page number
	bool is_new_page_allocated = false;

	unsigned int remain = length;
	unsigned int left_skip = sector_nb % SECTORS_PER_PAGE; // offset from start of page (when write to part of page)
	unsigned int right_skip;
	unsigned int write_sects;

	unsigned int ret = FTL_FAILURE;
	int write_page_nb=0;

	while (remain > 0)
	{
		if (remain > SECTORS_PER_PAGE - left_skip) // If left more then a page to write
		{
			right_skip = 0;
		}
		else
		{
			right_skip = SECTORS_PER_PAGE - left_skip - remain;
		}

		write_sects = SECTORS_PER_PAGE - left_skip - right_skip;

		// First try writing to the page without erasing it if it is program compatile (there is not need to flip bits from 0 to 1).
		_FTL_WRITE_DRY_SECT(lba, write_sects, data);
		if (_READ_STATUS_ENHANCED() == FTL_SUCCESS) {
			ret = _FTL_WRITE_COMMIT(lba, write_page_nb, write_sects, data);
		}
		else {
			ret = GET_NEW_PAGE(VICTIM_OVERALL, EMPTY_TABLE_ENTRY_NB, &new_ppn);
			if (ret == FTL_FAILURE) {
				RERR(FTL_FAILURE, "[FTL_WRITE] Get new page fail \n");
			}
			is_new_page_allocated = true;
			ret = SSD_PAGE_WRITE(CALC_FLASH(new_ppn), CALC_BLOCK(new_ppn), CALC_PAGE(new_ppn), write_page_nb, WRITE);
			uint64_t abs_physical_offset = new_ppn * GET_PAGE_SIZE() + lba % (int32_t)SECTORS_PER_PAGE;
			if (ret == FTL_SUCCESS && ssd_write(GET_FILE_NAME(), abs_physical_offset, length * GET_SECTOR_SIZE(), data) == SSD_FILE_OPS_SUCCESS) {
				ret = FTL_SUCCESS;
			}
		}

		//Calculate the logical page number -> the current sector_number / amount_of_sectors_per_page
		lpn = lba / (uint64_t)SECTORS_PER_PAGE;

		//we caused a block write -> update the logical block_write counter + update the physical block write counter
		wa_counters.logical_block_write_counter++;
		wa_counters.physical_block_write_counter++;
		//Send a physical write action being done to the statistics gathering
		if (ret == FTL_SUCCESS)
		{
			FTL_STATISTICS_GATHERING(GET_MAPPING_INFO(lpn) , PHYSICAL_WRITE);
		}
		write_page_nb++;

		//Send a logical write action being done to the statistics gathering
		FTL_STATISTICS_GATHERING(lpn , LOGICAL_WRITE);

		// logical page number to physical. will need to be changed to account for objectid
		if (_READ_STATUS_ENHANCED() != FTL_SUCCESS) {
			UPDATE_OLD_PAGE_MAPPING(lpn);
			UPDATE_NEW_PAGE_MAPPING(lpn, new_ppn);

		}

		if (ret == FTL_FAILURE) {
			PDBG_FTL("Error[FTL_WRITE] %d page write fail \n", GET_MAPPING_INFO(lpn));
		}

		lba += write_sects;
		remain -= write_sects;
		data += write_sects * GET_SECTOR_SIZE();
		left_skip = 0;
	}

	INCREASE_IO_REQUEST_SEQ_NB();

#ifdef GC_ON
	if (is_new_page_allocated) {
		GC_CHECK(CALC_FLASH(new_ppn), CALC_BLOCK(new_ppn), false, false); // is this a bug? gc will only happen on the last page's flash and block
	}
#endif

    PDBG_FTL("Complete\n");

	return ret;
}

//Get 2 physical page address, the source page which need to be moved to the destination page
ftl_ret_val _FTL_COPYBACK(uint64_t source, uint64_t destination)
{
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
        RDBG_FTL(FTL_FAILURE, "%u page copyback fail \n", source);

	// Actual page copy
	unsigned char buff[GET_PAGE_SIZE()];
	// If ssd_read failed - this is because we don't call _FTL_CREATE to create the ssd.img.
	// In this case we don't do anything - this is like happen before this change when the ssd.img was not used in the simulation.
	if (ssd_read(GET_FILE_NAME(), source * GET_PAGE_SIZE(), GET_PAGE_SIZE(), buff) == SSD_FILE_OPS_SUCCESS) {
		if (ssd_write(GET_FILE_NAME(), destination * GET_PAGE_SIZE(), GET_PAGE_SIZE(), buff) == SSD_FILE_OPS_ERROR) {
			// If ssd_read succeeded this fail shouldn't happen !!
			RDBG_FTL(FTL_FAILURE, "%u page copyback fail \n", source);
		}
	}

	//Handle page map
	lpn = GET_INVERSE_MAPPING_INFO(source);
	if (lpn != (uint64_t)-1)
	{
		//The given physical page is being map, the mapping information need to be changed
		UPDATE_OLD_PAGE_MAPPING(lpn); //as far as i can tell when being called under the gc manage all the actions are being done, but what if will be called from another place?
		UPDATE_NEW_PAGE_MAPPING(lpn, destination);
	}

	return ret;
}

ftl_ret_val _FTL_CREATE(void)
{
    // no "creation" in address-based storage
	return (ssd_create(GET_FILE_NAME(), SECTOR_NB * GET_SECTOR_SIZE()) == SSD_FILE_OPS_SUCCESS) ? FTL_SUCCESS : FTL_FAILURE;
}

ftl_ret_val _FTL_DELETE(void)
{
    // no "deletion" in address-based storage
    return FTL_SUCCESS;
}
