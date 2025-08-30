#include "common.h"
#include "ftl_sect_strategy.h"
#include "ssd_file_operations.h"

extern ssd_disk ssd;

static uint64_t physical_address_from_logical_address(uint8_t device_index, uint64_t lba, uint64_t* o_ppn);

static ftl_ret_val _READ_STATUS_ENHANCED(void);

#define FAILURE_VALUE UINT64_MAX
static uint64_t physical_address_from_logical_address(uint8_t device_index, uint64_t lba, uint64_t* o_ppn) {
	uint64_t lpn = lba / (int32_t)devices[device_index].sectors_per_page;
	uint64_t offset_in_page = lba % (int32_t)devices[device_index].sectors_per_page;
	uint64_t ppn = GET_MAPPING_INFO(device_index, lpn);
	if (o_ppn != NULL) {
		*o_ppn = ppn;
	}
	if (ppn == MAPPING_TABLE_INIT_VAL) return FAILURE_VALUE;
	return ppn * GET_PAGE_SIZE(device_index) + offset_in_page;
}

ftl_ret_val _FTL_READ(uint8_t device_index, uint64_t sector_nb, unsigned int length, unsigned char *data)
{
	return _FTL_READ_SECT(device_index, sector_nb, length, data);
}

ftl_ret_val _FTL_READ_SECT(uint8_t device_index, uint64_t sector_nb, unsigned int length, unsigned char *data)
{

	PDBG_FTL("Start: sector_nb %ld length %u\n", sector_nb, length);

	if (sector_nb + length > (uint64_t)devices[device_index].sectors_per_page * (uint64_t)devices[device_index].page_nb *
			(uint64_t)devices[device_index].block_nb * (uint64_t)devices[device_index].flash_nb)
		RERR(FTL_FAILURE, "[FTL_READ] Exceed Sector number\n");

	uint64_t lpn;
	uint64_t ppn;
	uint64_t lba = sector_nb;
	unsigned int remain = length;
	unsigned long left_skip = sector_nb % devices[device_index].sectors_per_page;
	unsigned long right_skip;
	unsigned int read_sects;

	unsigned int ret = FTL_FAILURE;
	int read_page_nb = 0;
	int io_page_nb;

	// just calculate the overhead of allocating the request. io_page_nb will be the total number of pages we're gonna read
	ssds_manager[device_index].io_alloc_overhead = ALLOC_IO_REQUEST(device_index, sector_nb, length, READ, &io_page_nb);

	remain = length;
	lba = sector_nb;
	left_skip = sector_nb % devices[device_index].sectors_per_page;

	while (remain > 0)
	{
		if (remain > devices[device_index].sectors_per_page - left_skip)
		{
			right_skip = 0;
		}
		else
		{
			right_skip = devices[device_index].sectors_per_page - left_skip - remain;
		}

		read_sects = devices[device_index].sectors_per_page - left_skip - right_skip;

		lpn = lba / (int32_t)devices[device_index].sectors_per_page;
		//Send a logical read action being done to the statistics gathering
		FTL_STATISTICS_GATHERING(device_index, lpn , LOGICAL_READ);

		// Calculate absolute physical offset and ppn from lba
		size_t abs_physical_offset = physical_address_from_logical_address(device_index, lba, &ppn);

		if (ppn == MAPPING_TABLE_INIT_VAL || abs_physical_offset == FAILURE_VALUE) {
			RDBG_FTL(FTL_FAILURE, "No Mapping info\n");
		}

		ret = SSD_PAGE_READ(device_index, CALC_FLASH(device_index, ppn), CALC_BLOCK(device_index, ppn), CALC_PAGE(device_index, ppn), read_page_nb, READ);
		//Send a physical read action being done to the statistics gathering
		if (ret == FTL_SUCCESS)
		{
			FTL_STATISTICS_GATHERING(device_index, ppn , PHYSICAL_READ);
			if (data != NULL &&
				ssd_read(GET_FILE_NAME(device_index), abs_physical_offset, read_sects * GET_SECTOR_SIZE(device_index), data) != SSD_FILE_OPS_SUCCESS) {
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
		if (data != NULL) {
			data += read_sects * GET_SECTOR_SIZE(device_index);
		}

		// Normally, there would be a LOG_LOGICAL_CELL_READ call here. As it happens, each physical read
		// corresponds exactly to one logical read with the exact same parameters; thus, we omit the logical
		// read log all together, and refer to the physical one as an indication to both.
	}

	INCREASE_IO_REQUEST_SEQ_NB();

	PDBG_FTL("Complete\n");

	return ret;
}

ftl_ret_val _FTL_WRITE(uint8_t device_index, uint64_t sector_nb, unsigned int length, const unsigned char *data)
{
    return _FTL_WRITE_SECT(device_index, sector_nb, length, data);
}

// **
// * The following functions are internal helper functions for the use of _FTL_WRITE_SECT
// **
static ftl_ret_val read_status_enanched = FTL_FAILURE;

// Checks if a writing is program compatible (there is not actuall writing here).
// NOTE: We assume the parameters are for writing in a single page amount of `length` sectors.
static void _FTL_WRITE_DRY_SECT(uint8_t device_index, uint64_t lba, unsigned int length, const unsigned char *data) {
	read_status_enanched = FTL_FAILURE;
	if (data == NULL) {
		read_status_enanched = FTL_FAILURE;
		return;
	}

	size_t abs_physical_offset =  physical_address_from_logical_address(device_index, lba, NULL);
	if (abs_physical_offset == FAILURE_VALUE) {
		read_status_enanched = FTL_FAILURE;
		return;
	}

	if (is_program_compatible(GET_FILE_NAME(device_index), abs_physical_offset, length * GET_SECTOR_SIZE(device_index), data)) {
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
static ftl_ret_val _FTL_WRITE_COMMIT(uint8_t device_index, uint64_t lba, int write_page_nb, unsigned int length, const unsigned char *data) {
	if (data == NULL) {
		return FTL_FAILURE;
	}

	uint64_t ppn = MAPPING_TABLE_INIT_VAL;
	size_t abs_physical_offset = physical_address_from_logical_address(device_index, lba, &ppn);
	if (abs_physical_offset == FAILURE_VALUE || ppn == MAPPING_TABLE_INIT_VAL) {
		return FTL_FAILURE;
	}
	ftl_ret_val ret = SSD_PAGE_WRITE(device_index, CALC_FLASH(device_index, ppn), CALC_BLOCK(device_index, ppn), CALC_PAGE(device_index, ppn), write_page_nb, WRITE_COMMIT);
	if (ret == FTL_SUCCESS && ssd_write(GET_FILE_NAME(device_index), abs_physical_offset, length * GET_SECTOR_SIZE(device_index), data) == SSD_FILE_OPS_SUCCESS) {
		return FTL_SUCCESS;
	}

	return FTL_FAILURE;
}
// **
// * End of helper functions for the use of _FTL_WRITE_SECT
// **

ftl_ret_val _FTL_WRITE_SECT(uint8_t device_index, uint64_t sector_nb, unsigned int length, const unsigned char *data)
{
	PDBG_FTL("Start: sector_nb %" PRIu64 "length %u\n", sector_nb, length);

	int io_page_nb;

	if (sector_nb + length > (uint64_t)devices[device_index].sectors_per_page * (uint64_t)devices[device_index].page_nb *
			(uint64_t)devices[device_index].block_nb * (uint64_t)devices[device_index].flash_nb)
		RERR(FTL_FAILURE, "Exceed Sector number\n");

	ssds_manager[device_index].io_alloc_overhead = ALLOC_IO_REQUEST(device_index, sector_nb, length, WRITE, &io_page_nb);

	uint64_t lba = sector_nb; // logical block address
	uint64_t lpn;			  // logical page number
	uint64_t new_ppn = MAPPING_TABLE_INIT_VAL; // physical page number
	bool is_new_page_allocated = false;

	unsigned int remain = length;
	unsigned int left_skip = sector_nb % devices[device_index].sectors_per_page; // offset from start of page (when write to part of page)
	unsigned int right_skip;
	unsigned int write_sects;

	unsigned int ret = FTL_FAILURE;
	int write_page_nb=0;

	while (remain > 0)
	{
		if (remain > devices[device_index].sectors_per_page - left_skip) // If left more then a page to write
		{
			right_skip = 0;
		}
		else
		{
			right_skip = devices[device_index].sectors_per_page - left_skip - remain;
		}

		write_sects = devices[device_index].sectors_per_page - left_skip - right_skip;

		//Calculate the logical page number -> the current sector_number / amount_of_sectors_per_page
		lpn = lba / (uint64_t)devices[device_index].sectors_per_page;

		// First try writing to the page without erasing it if it is program compatile (there is not need to flip bits from 0 to 1).
		_FTL_WRITE_DRY_SECT(device_index, lba, write_sects, data);
		if (_READ_STATUS_ENHANCED() == FTL_SUCCESS) {
			ret = _FTL_WRITE_COMMIT(device_index, lba, write_page_nb, write_sects, data);
		}
		else {
			ret = GET_NEW_PAGE(device_index, VICTIM_OVERALL, devices[device_index].empty_table_entry_nb, &new_ppn);
			if (ret == FTL_FAILURE) {
				RERR(FTL_FAILURE, "[FTL_WRITE] Get new page fail \n");
			}
			is_new_page_allocated = true;
			ret = SSD_PAGE_WRITE(device_index, CALC_FLASH(device_index, new_ppn), CALC_BLOCK(device_index, new_ppn), CALC_PAGE(device_index, new_ppn), write_page_nb, WRITE);
			uint64_t abs_physical_offset = new_ppn * GET_PAGE_SIZE(device_index) + lba % (int32_t)devices[device_index].sectors_per_page;
			if (ret == FTL_SUCCESS && data != NULL &&
				ssd_write(GET_FILE_NAME(device_index), abs_physical_offset, write_sects * GET_SECTOR_SIZE(device_index), data) == SSD_FILE_OPS_SUCCESS) {
				ret = FTL_SUCCESS;
			}

			// logical page number to physical. will need to be changed to account for objectid
			UPDATE_OLD_PAGE_MAPPING(device_index, lpn);
			UPDATE_NEW_PAGE_MAPPING(device_index, lpn, new_ppn);
		}

		//we caused a block write -> update the logical block_write counter + update the physical block write counter
		wa_counters.logical_block_write_counter++;
		wa_counters.physical_block_write_counter++;
		//Send a physical write action being done to the statistics gathering
		if (ret == FTL_SUCCESS)
		{
			FTL_STATISTICS_GATHERING(device_index, GET_MAPPING_INFO(device_index, lpn) , PHYSICAL_WRITE);
		}
		write_page_nb++;

		//Send a logical write action being done to the statistics gathering
		FTL_STATISTICS_GATHERING(device_index, lpn , LOGICAL_WRITE);

		if (ret == FTL_FAILURE) {
			PDBG_FTL("Error[FTL_WRITE] %d page write fail \n", GET_MAPPING_INFO(device_index, lpn));
		}

		lba += write_sects;
		remain -= write_sects;
		if (data != NULL) {
			data += write_sects * GET_SECTOR_SIZE(device_index);
		}
		left_skip = 0;
	}

	INCREASE_IO_REQUEST_SEQ_NB();

#ifdef GC_ON
	if (is_new_page_allocated) {
		GC_CHECK(device_index, CALC_FLASH(device_index, new_ppn),
			CALC_BLOCK(device_index, new_ppn), false, false); // is this a bug? gc will only happen on the last page's flash and block
	}
#endif

    PDBG_FTL("Complete\n");

	return ret;
}

//Get 2 physical page address, the source page which need to be moved to the destination page
ftl_ret_val _FTL_COPYBACK(uint8_t device_index, uint64_t source, uint64_t destination)
{
	uint64_t lpn; //The logical page address, the page that being moved.
	unsigned int ret = FTL_FAILURE;

	//Handle copyback delays
	ret = SSD_PAGE_COPYBACK(device_index, source, destination, COPYBACK);

    // actual page swap, go korea
    /*SSD_PAGE_READ(CALC_FLASH(source), CALC_BLOCK(source), CALC_PAGE(source), 0, GC_READ);
    SSD_PAGE_WRITE(CALC_FLASH(destination), CALC_BLOCK(destination), CALC_PAGE(destination), 0, GC_WRITE);
    lpn = GET_INVERSE_MAPPING_INFO(source);
    UPDATE_NEW_PAGE_MAPPING(lpn, destination);*/


	if (ret == FTL_FAILURE)
        RDBG_FTL(FTL_FAILURE, "%u page copyback fail \n", source);

	// Actual page copy
	unsigned char buff[GET_PAGE_SIZE(device_index)];
	// If ssd_read failed - this is because we don't call _FTL_CREATE to create the ssd.img.
	// In this case we don't do anything - this is like happen before this change when the ssd.img was not used in the simulation.
	if (ssd_read(GET_FILE_NAME(device_index), source * GET_PAGE_SIZE(device_index), GET_PAGE_SIZE(device_index), buff) == SSD_FILE_OPS_SUCCESS) {
		if (ssd_write(GET_FILE_NAME(device_index), destination * GET_PAGE_SIZE(device_index), GET_PAGE_SIZE(device_index), buff) == SSD_FILE_OPS_ERROR) {
			// If ssd_read succeeded this fail shouldn't happen !!
			RDBG_FTL(FTL_FAILURE, "%u page copyback fail \n", source);
		}
	}

	//Handle page map
	GET_INVERSE_MAPPING_INFO(device_index, source, &lpn);

	if (lpn != MAPPING_TABLE_INIT_VAL)
	{
		// The given physical page is being map, the mapping information need to be changed,
		UPDATE_OLD_PAGE_MAPPING(device_index, lpn); //as far as I can tell when being called under the gc manage all the actions are being done, but what if will be called from another place?
		UPDATE_NEW_PAGE_MAPPING(device_index, lpn, destination);
	}

	return ret;
}

ftl_ret_val _FTL_CREATE(uint8_t device_index)
{
    // no "creation" in address-based storage
	return (ssd_create(GET_FILE_NAME(device_index), (uint64_t)devices[device_index].sectors_per_page * (uint64_t)devices[device_index].page_nb *
			(uint64_t)devices[device_index].block_nb * (uint64_t)devices[device_index].flash_nb * GET_SECTOR_SIZE(device_index))
				== SSD_FILE_OPS_SUCCESS) ? FTL_SUCCESS : FTL_FAILURE;
}

ftl_ret_val _FTL_DELETE(void)
{
    // no "deletion" in address-based storage
    return FTL_SUCCESS;
}
