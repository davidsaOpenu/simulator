#include "common.h"
#include "ftl_sect_strategy.h"

extern ssd_disk ssd;

static bool is_valid_device_index(uint8_t device_index) {
	return devices != NULL && device_index < device_count;
}

ftl_ret_val _FTL_READ(uint8_t device_index, uint64_t sector_nb, unsigned int length, unsigned char *data)
{
	return _FTL_READ_SECT(device_index, sector_nb, length, data);
}

ftl_ret_val _FTL_READ_SECT(uint8_t device_index, uint64_t sector_nb, unsigned int length, unsigned char *data)
{
	if (!is_valid_device_index(device_index)) {
		RERR(FTL_FAILURE, "Invalid device index %u (device_count=%u)\n",
			(unsigned int)device_index, (unsigned int)device_count);
	}

	if (devices[device_index].storage_strategy != STRATEGY_SECTOR) {
		DEV_RERR(FTL_FAILURE, device_index, "wrong storage strategy %d\n", devices[device_index].storage_strategy);
	}

	PDBG_FTL("Start: sector_nb %ld length %u\n", sector_nb, length);

	if (sector_nb + length > devices[device_index].sectors_in_ssd)
		RERR(FTL_FAILURE, "[FTL_READ] Exceed Sector number\n");

	uint64_t lpn;
	uint64_t ppn;
	uint64_t lba = sector_nb;
	unsigned int remain = length;
	unsigned long left_skip = sector_nb % devices[device_index].sectors_per_page;
	unsigned long right_skip;
	unsigned int read_sects;
	size_t amount_of_bytes_to_read;
	uint64_t offset_in_page;

	unsigned int ret = FTL_FAILURE;
	int read_page_nb = 0;

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
		amount_of_bytes_to_read = read_sects * GET_SECTOR_SIZE(device_index);

		lpn = lba / (int32_t)devices[device_index].sectors_per_page;
		//Send a logical read action being done to the statistics gathering
		FTL_STATISTICS_GATHERING(device_index, lpn , LOGICAL_READ);

		offset_in_page = lba % (int32_t)devices[device_index].sectors_per_page;
		ppn = GET_MAPPING_INFO(device_index, lpn);
        if (ppn == MAPPING_TABLE_INIT_VAL)
        {
            RDBG_FTL(FTL_FAILURE, "No Mapping info\n");
        }

		// ONFI doesn't allow data to be NULL, but FTL does.
		// Therefore, in order to keep the statistics in check, in that case we call SSD_PAGE_READ directly.
        if (data != NULL)
        {
            size_t nread = 0;
            onfi_ret_val onfi_ret = ONFI_READ(device_index, ppn, offset_in_page, data, amount_of_bytes_to_read, &nread);
            // Send a physical read action being done to the statistics gathering
            if (onfi_ret == ONFI_SUCCESS)
            {
                ret = FTL_SUCCESS;
                FTL_STATISTICS_GATHERING(device_index, ppn, PHYSICAL_READ);
            }

            if (onfi_ret == ONFI_FAILURE || nread != amount_of_bytes_to_read)
            {
                ret = FTL_FAILURE;
            }
        }
        else
        { // Only for statistics gathering without an actual reading of data.
            ret = SSD_PAGE_READ(device_index, CALC_FLASH(device_index, ppn), CALC_BLOCK(device_index, ppn), CALC_PAGE(device_index, ppn), read_page_nb, READ);
            // Send a physical read action being done to the statistics gathering
            if (ret == FTL_SUCCESS)
            {
                FTL_STATISTICS_GATHERING(device_index, ppn, PHYSICAL_READ);
            }
        }

#ifdef FTL_DEBUG
        if (ret == FTL_FAILURE)
            PERR("%zu page read fail \n", ppn);
#endif
		read_page_nb++;

		lba += read_sects;
		remain -= read_sects;
		left_skip = 0;
		if (data != NULL) {
			data += amount_of_bytes_to_read;
		}

		// Normally, there would be a LOG_LOGICAL_CELL_READ call here. As it happens, each physical read
		// corresponds exactly to one logical read with the exact same parameters; thus, we omit the logical
		// read log all together, and refer to the physical one as an indication to both.
	}

	PDBG_FTL("Complete\n");

	return ret;
}

ftl_ret_val FTL_READ_SECT(uint8_t device_index, uint64_t sector_nb, unsigned int length, unsigned char *data)
{
	if (!is_valid_device_index(device_index)) {
		return _FTL_READ_SECT(device_index, sector_nb, length, data);
	}
	LOCK_DEVICE(device_index);
	ftl_ret_val ret = _FTL_READ_SECT(device_index, sector_nb, length, data);
	UNLOCK_DEVICE(device_index);
	return ret;
}

ftl_ret_val _FTL_WRITE(uint8_t device_index, uint64_t sector_nb, unsigned int length, const unsigned char *data)
{
    return _FTL_WRITE_SECT(device_index, sector_nb, length, data);
}

ftl_ret_val _FTL_WRITE_SECT(uint8_t device_index, uint64_t sector_nb, unsigned int length, const unsigned char *data)
{
	if (!is_valid_device_index(device_index)) {
		RERR(FTL_FAILURE, "Invalid device index %u (device_count=%u)\n",
			(unsigned int)device_index, (unsigned int)device_count);
	}

	if (devices[device_index].storage_strategy != STRATEGY_SECTOR) {
		DEV_RERR(FTL_FAILURE, device_index, "wrong storage strategy %d\n", devices[device_index].storage_strategy);
	}

	PDBG_FTL("Start: sector_nb %ld length %u\n", sector_nb, length);

	if (sector_nb + length > devices[device_index].sectors_in_ssd)
		RERR(FTL_FAILURE, "[FTL_WRITE] Exceed Sector number\n");

	uint64_t lba = sector_nb; // logical block address
	uint64_t lpn;			  // logical page number
	uint64_t offset_in_page;
	uint64_t new_ppn = MAPPING_TABLE_INIT_VAL; // physical page number
	bool device_full = false;

	unsigned int remain = length;
	unsigned int left_skip = sector_nb % devices[device_index].sectors_per_page; // offset from start of page (when write to part of page)
	unsigned int right_skip;
	unsigned int write_sects;
	size_t amount_of_bytes_to_write;

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
		amount_of_bytes_to_write = write_sects * GET_SECTOR_SIZE(device_index);

		// Calculate the logical page number -> the current sector_number / amount_of_sectors_per_page
		lpn = lba / (uint64_t)devices[device_index].sectors_per_page;

		// Calculate the offset inside the page
		offset_in_page = lba % (int32_t)devices[device_index].sectors_per_page;

		ret = GET_NEW_PAGE(device_index, VICTIM_OVERALL, devices[device_index].empty_table_entry_nb, &new_ppn);
		if (ret == FTL_FAILURE) {
			ret = GET_NEW_PAGE(device_index, VICTIM_OVERALL_GC, devices[device_index].empty_table_entry_nb, &new_ppn);
			if (ret == FTL_FAILURE) {
				RERR(FTL_FAILURE, "[FTL_WRITE] Get new page fail \n");
			} else {
				device_full = true;
				DEV_PINFO(device_index, "[FTL_WRITE] obtained a GC reserved page because device is full\n");
			}
		}

		// ONFI doesn't allow data to be NULL, but FTL does.
		// Therefore, in order to keep the statistics in check, in that case we call SSD_PAGE_WRITE directly.
		if (data != NULL) {
			size_t nwritten = 0;
			onfi_ret_val onfi_ret = ONFI_PAGE_PROGRAM(device_index, new_ppn, offset_in_page, data, amount_of_bytes_to_write, &nwritten);
			ret = (onfi_ret == ONFI_SUCCESS && nwritten == amount_of_bytes_to_write) ? FTL_SUCCESS : FTL_FAILURE;
		} else { // Only for statistics gathering without an actual writing of data.
			ret = SSD_PAGE_WRITE(device_index, CALC_FLASH(device_index, new_ppn), CALC_BLOCK(device_index, new_ppn), CALC_PAGE(device_index, new_ppn), write_page_nb, WRITE);
		}

		// logical page number to physical. will need to be changed to account for objectid
		UPDATE_OLD_PAGE_MAPPING(device_index, lpn);
		UPDATE_NEW_PAGE_MAPPING(device_index, lpn, new_ppn);

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
			data += amount_of_bytes_to_write;
		}
		left_skip = 0;
	}

#ifdef GC_ON
	if (device_full) {
		GC_CHECK(device_index, true, false);
	}
#endif

    PDBG_FTL("Complete\n");

	return ret;
}

ftl_ret_val FTL_WRITE_SECT(uint8_t device_index, uint64_t sector_nb, unsigned int length, const unsigned char *data)
{
	if (!is_valid_device_index(device_index)) {
		return _FTL_WRITE_SECT(device_index, sector_nb, length, data);
	}
	LOCK_DEVICE(device_index);
	ftl_ret_val ret = _FTL_WRITE_SECT(device_index, sector_nb, length, data);
	UNLOCK_DEVICE(device_index);
	return ret;
}

//Get 2 physical page address, the source page which need to be moved to the destination page
ftl_ret_val _FTL_COPYBACK(uint8_t device_index, uint64_t source, uint64_t destination, int type)
{
	if (devices[device_index].storage_strategy != STRATEGY_SECTOR) {
		DEV_RERR(FTL_FAILURE, device_index, "wrong storage strategy %d\n", devices[device_index].storage_strategy);
	}

	uint64_t lpn; //The logical page address, the page that being moved.
	unsigned int ret = FTL_FAILURE;

	//Handle copyback delays
	ret = SSD_PAGE_COPYBACK(device_index, source, destination, type);

    // actual page swap, go korea
    /*SSD_PAGE_READ(CALC_FLASH(source), CALC_BLOCK(source), CALC_PAGE(source), 0, GC_READ);
    SSD_PAGE_WRITE(CALC_FLASH(destination), CALC_BLOCK(destination), CALC_PAGE(destination), 0, GC_WRITE);
    lpn = GET_INVERSE_MAPPING_INFO(source);
    UPDATE_NEW_PAGE_MAPPING(lpn, destination);*/


	if (ret == FTL_FAILURE)
        RDBG_FTL(FTL_FAILURE, "%u page copyback fail \n", source);

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
