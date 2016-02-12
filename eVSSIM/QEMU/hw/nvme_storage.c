/*
 * Copyright (c) 2011 Intel Corporation
 *
 * by
 *    Maciej Patelczyk <mpatelcz@gkslx007.igk.intel.com>
 *    Krzysztof Wierzbicki <krzysztof.wierzbicki@intel.com>
 *    Patrick Porlan <patrick.porlan@intel.com>
 *    Nisheeth Bhat <nisheeth.bhat@intel.com>
 *    Sravan Kumar Thokala <sravan.kumar.thokala@intel.com>
 *    Keith Busch <keith.busch@intel.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */

#include "nvme.h"
#include "nvme_debug.h"

#ifdef CONFIG_VSSIM
#include "vssim_config_manager.h"
#include "ftl_obj_strategy.h"
#include "ftl_sect_strategy.h"

//extern char* GET_FILE_NAME(void);
//TODO: review SECTOR_SIZE vs. page_size of the NVME
//extern int SECTOR_SIZE; // SSD configuration. See CONFIG/vssim_config_manager
#endif /* CONFIG_VSSIM */

#include <sys/mman.h>
#include <assert.h>


#define MASK_AD         0x4
#define MASK_IDW        0x2
#define MASK_IDR        0x1

static uint8_t read_dsm_ranges(uint64_t range_prp1, uint64_t range_prp2,
    uint8_t *buffer_addr, uint64_t *data_size_p);
static void dsm_dealloc(DiskInfo *disk, uint64_t slba, uint64_t nlb);


static void parse_metadata(uint8_t *metadata_mapping_address, unsigned int metadata_size, object_location *obj_loc)
{

	char MAGIC[] = "eVSSIM_MAGIC";
	int MAGIC_LENGTH = 12;

	char* asACharArray = (char*)metadata_mapping_address;
	char* magicSuffixPtr = NULL;


	LOG_DBG("metadata size is: %d\n", metadata_size);
	LOG_DBG("metadata addr is: %p\n", asACharArray);

	if (!memcmp(MAGIC, asACharArray,MAGIC_LENGTH))
	{
		asACharArray += MAGIC_LENGTH;
		LOG_DBG("Found magic at %p\n", asACharArray);
		magicSuffixPtr = strchr(asACharArray, '!');
		if (magicSuffixPtr)
		{
			LOG_DBG("Found suffix!\n");
			char* seperatorPtr = strchr(asACharArray, '_');

			if (seperatorPtr != NULL)
			{
				LOG_DBG("Found seperator !\n");

				*seperatorPtr = '\x00';
				*magicSuffixPtr = '\x00';

				obj_loc->partition_id =  atoi(asACharArray);
				obj_loc->object_id = atoi(seperatorPtr+1);

				*seperatorPtr = '_';
				*magicSuffixPtr = '!';
			}

			LOG_DBG("partition id is: %lu object id is: %lu\n", obj_loc->partition_id, obj_loc->object_id);
		}

	}

}

void nvme_dma_mem_read(target_phys_addr_t addr, uint8_t *buf, int len)
{
    cpu_physical_memory_rw(addr, buf, len, 0);
}

void nvme_dma_mem_write(target_phys_addr_t addr, uint8_t *buf, int len)
{
    cpu_physical_memory_rw(addr, buf, len, 1);
}

#ifdef CONFIG_VSSIM
//nvme_dma_mem_read2() -> READ what's inside the prp (addr) and write it to the hw (buf + relevant offset according to how the prps were divided) --> in this case, the prp (addr) is the dma memory we read from ?
static void nvme_dma_mem_read2(target_phys_addr_t addr, uint8_t *buf, int len,
        uint8_t *mapping_addr, unsigned int partition_id, unsigned int object_id, bool should_create_obj)
{
    if((len % eVSSIM_SECTOR_SIZE) != 0){
        LOG_ERR("nvme_dma_mem_read2: len (=%d) %% %d == %d (should be 0)",
                len, eVSSIM_SECTOR_SIZE, (len % eVSSIM_SECTOR_SIZE));
    }
    if((buf - mapping_addr) % eVSSIM_SECTOR_SIZE != 0){
        LOG_ERR("nvme_dma_mem_read2: (buf - mapping_addr) (=%ld) %% %d == %ld "
                "(should be 0)",
                buf - mapping_addr, eVSSIM_SECTOR_SIZE,
                (buf - mapping_addr) % eVSSIM_SECTOR_SIZE);
    }

    if (partition_id != 0 && object_id != 0)
    {

    	LOG_DBG("object strategy\n");
    	object_location obj_loc = {
    			.object_id = object_id,
				.partition_id = partition_id,
				.create_object = should_create_obj
    	};

    	//We don't need to specify the parition_id + object id
    	//as they're determined automatically by the
    	//simulator methods. However, I'm leaving this here
    	//as infrastructure if we'd like to change this in the future
    	//and set the IDs ourselves
    	_FTL_OBJ_WRITECREATE(obj_loc, len);

    	//write the object data to the persistent osd storage
    	//
    	//We DO use the obj_loc IDs here... they're NOT auto-determined
    	//
    	//In order to write the data to the OSD object, we need to go over the prp data at addr and write it to the
    	//object (quite the same as it's written to buf in the sector strategy below -> we might be able to just read buf, as it'll be easier to read than reading addr, which is a physical address)

    	//Following the comment above, Trying to read from addr to buf and then writing buf
    	cpu_physical_memory_rw(addr, buf, len, 0);
    	OSD_WRITE_OBJ(obj_loc, len, buf);
    }
    else
    {
    	LOG_DBG("sector strategy\n");
    	//sector strategy -> continue normally
    	_FTL_WRITE_SECT( (buf - mapping_addr) / eVSSIM_SECTOR_SIZE, len / eVSSIM_SECTOR_SIZE);
    	//read from dma memory (prp) and write to qemu's volatile memory
    	cpu_physical_memory_rw(addr, buf, len, 0);
    }

}

//nvme_dma_mem_write2() -> read from the hw (buf + relevant offset according to the current prp number we're reading ?) and WRITE to the prp (dma addr)
static void nvme_dma_mem_write2(target_phys_addr_t addr, uint8_t *buf, int len,
        uint8_t *mapping_addr, unsigned int partition_id, unsigned int object_id)
{
	//the buf pointer is actually -> buf = mapping_addr + offset so:
	uint64_t offset = buf - mapping_addr;

    if((len % eVSSIM_SECTOR_SIZE) != 0){
        LOG_ERR("nvme_dma_mem_write2: len (=%d) %% %d == %d (should be 0)",
                len, eVSSIM_SECTOR_SIZE, (len % eVSSIM_SECTOR_SIZE));
    }
    if((offset) % eVSSIM_SECTOR_SIZE != 0){
        LOG_ERR("nvme_dma_mem_write2: (offset) (=%ld) %% %d == %ld "
                "(should be 0)",
				offset, eVSSIM_SECTOR_SIZE,
                (offset) % eVSSIM_SECTOR_SIZE);
    }

    if (partition_id != 0 && object_id != 0)
    {

    	LOG_DBG("object strategy\n");
    	object_location obj_loc = {
    			.object_id = object_id,
				.partition_id = partition_id,
    	};

    	//perform a simulator READ
    	//
    	//We have a problem here as we have no idea what object id was created in the simulator object write (auto incremented) -> so this should fail as it's not the same as the
    	//object id we're about to read from the osd later
        int ret = _FTL_OBJ_READ(obj_loc.object_id, 0, len);
        if (!ret)
        	LOG_DBG("FTL_OBJ_READ failed with ret: %d\n", ret);
        else
        	LOG_DBG("Successfully read object from simulator\n");

        //read from persistent OSD storage
    	OSD_READ_OBJ(obj_loc, len, addr, offset);
    }
    else
    {
    	LOG_DBG("sector strategy\n");
    	//sector strategy -> continue normally
        //_FTL_READ_SECT((buf - mapping_addr) / eVSSIM_SECTOR_SIZE, len / eVSSIM_SECTOR_SIZE);
    	_FTL_READ_SECT(len / eVSSIM_SECTOR_SIZE, (buf - mapping_addr) / eVSSIM_SECTOR_SIZE);
    	//read from qemu's volatile memory and write to dma memory (prp)
        cpu_physical_memory_rw(addr, buf, len, 1);
    }
}
#endif /* CONFIG_VSSIM */

static uint8_t do_rw_prp(NVMEState *n, uint64_t mem_addr, uint64_t *data_size_p,
    uint64_t *file_offset_p, uint8_t *mapping_addr, uint8_t rw, object_location obj_loc)
{
    uint64_t data_len;

    if (*data_size_p == 0) {
        return FAIL;
    }

    /* Data Len to be written per page basis */
    //
    //the data_len is affected by the prp memory address - according to its alignment to page size
    //this calculation of data_len appears to be simulating the mechanism in the kernel which sets up the data size for each prp prps -> it does that according to prp alignment to a page size
    //
    //if the calculated data_len is bigger than the provided data_size(which is a multplication of block size), then we'll use the provided data_size as the value of data_len (as there's no point in writing more data than provided)
    data_len = PAGE_SIZE - (mem_addr % PAGE_SIZE);
    LOG_DBG("data_len: %lu mem_addr mod PAGE_SIZE: %lu data_size: %lu\n", data_len, mem_addr % PAGE_SIZE, *data_size_p);
    if (data_len > *data_size_p) {
        data_len = *data_size_p;
    }

    LOG_DBG("File offset for read/write:%ld", *file_offset_p);
    LOG_DBG("Length for read/write:%ld (0x%016lX)", data_len, data_len);
    LOG_DBG("Address for read/write:%ld (0x%016lX))", mem_addr, mem_addr);

    switch (rw) {
    case NVME_CMD_READ:
        LOG_DBG("Read cmd called");
#ifdef CONFIG_VSSIM
        nvme_dma_mem_write2(mem_addr, (mapping_addr + *file_offset_p), data_len, mapping_addr, obj_loc.partition_id, obj_loc.object_id);
#else
        nvme_dma_mem_write(mem_addr, (mapping_addr + *file_offset_p), data_len);
#endif
        break;
    case NVME_CMD_WRITE:
        LOG_DBG("Write cmd called");
#ifdef CONFIG_VSSIM
        nvme_dma_mem_read2(mem_addr, (mapping_addr + *file_offset_p), data_len, mapping_addr, obj_loc.partition_id, obj_loc.object_id, obj_loc.create_object);
#else
        nvme_dma_mem_read(mem_addr, (mapping_addr + *file_offset_p), data_len);
#endif
        break;
    default:
        LOG_ERR("Error- wrong opcode: %d", rw);
        return FAIL;
    }

    printMemoryDump(mapping_addr + *file_offset_p,data_len);

    *file_offset_p = *file_offset_p + data_len;
    *data_size_p = *data_size_p - data_len;
    return NVME_SC_SUCCESS;
}

static uint8_t do_rw_prp_list(NVMEState *n, NVMECmd *command,
    uint64_t *data_size_p, uint64_t *file_offset_p, uint8_t *mapping_addr, object_location obj_loc)
{
    uint64_t prp_list[512], prp_entries;
    uint16_t i = 0;
    uint8_t res = FAIL;
    NVME_rw *cmd = (NVME_rw *)command;

    LOG_DBG("Data Size remaining for read/write:%ld", *data_size_p);

    /* Logic to find the number of PRP Entries */
    prp_entries = (uint64_t) ((*data_size_p + PAGE_SIZE - 1) / PAGE_SIZE);
    //no need for nvme_dma_mem_read2 here as it's only reading prp2 (dma memory) into prp_list -> it's not writing to the qemu simulated hw
    nvme_dma_mem_read(cmd->prp2, (uint8_t *)prp_list, min(sizeof(prp_list), prp_entries * sizeof(uint64_t)));

    /* Read/Write on PRPList */
    while (*data_size_p != 0) {
        if (i == 511 && *data_size_p > PAGE_SIZE) {
            /* Calculate the actual number of remaining entries */
            prp_entries = (uint64_t) ((*data_size_p + PAGE_SIZE - 1) / PAGE_SIZE);
            //reading the last item in the prp_list to prp_list (not the qemu simulated hw, so no need for nvme_dma_mem_read2)
            nvme_dma_mem_read(prp_list[511], (uint8_t *)prp_list, min(sizeof(prp_list), prp_entries * sizeof(uint64_t)));
            i = 0;
        }

        res = do_rw_prp(n, prp_list[i], data_size_p, file_offset_p, mapping_addr, cmd->opcode, obj_loc);
        LOG_DBG("Data Size remaining for read/write:%ld", *data_size_p);
        if (res == FAIL) {
            break;
        }
        i++;
    }
    return res;

}

/*********************************************************************
    Function     :    update_ns_util
    Description  :    Updates the Namespace Utilization
                      of NVME disk
    Return Type  :    void

    Arguments    :    NVMEState * : Pointer to NVME device State
                      struct NVME_rw * : NVME IO command
*********************************************************************/
static void update_ns_util(DiskInfo *disk, uint64_t slba, uint64_t nlb)
{
    uint64_t index;

    /* Update the namespace utilization */
    for (index = slba; index <= nlb + slba; index++) {
        if (!((disk->ns_util[index / 8]) & (1 << (index % 8)))) {
            disk->ns_util[(index / 8)] |= (1 << (index % 8));
            disk->idtfy_ns.nuse++;
        }
    }
}

/*********************************************************************
    Function     :    nvme_update_stats
    Description  :    Updates the Namespace Utilization and enqueues
                      async smart log information for NVME disk
    Return Type  :    void

    Arguments    :    NVMEState * : Pointer to NVME device State
                      DiskInfo  * : Pointer to disk info
                      uint8_t     : Cmd opcode
                      uint64_t    : starting LBA
                      uint64_t    : Number of LBA
*********************************************************************/
static void nvme_update_stats(NVMEState *n, DiskInfo *disk, uint8_t opcode,
    uint64_t slba, uint64_t nlb)
{
    uint64_t tmp;
    if (opcode == NVME_CMD_WRITE) {
        uint64_t old_use = disk->idtfy_ns.nuse;

        update_ns_util(disk, slba, nlb);

        /* check if there needs to be an event issued */
        if (old_use != disk->idtfy_ns.nuse && !disk->thresh_warn_issued &&
                (100 - (uint32_t)((((double)disk->idtfy_ns.nuse) /
                    disk->idtfy_ns.nsze) * 100) < NVME_SPARE_THRESH)) {
            LOG_NORM("Device:%d nsid:%d, setting threshold warning",
                n->instance, disk->nsid);
            disk->thresh_warn_issued = 1;
            enqueue_async_event(n, event_type_smart,
                event_info_smart_spare_thresh, NVME_LOG_SMART_INFORMATION);
        }

        if (++disk->host_write_commands[0] == 0) {
            ++disk->host_write_commands[1];
        }
        disk->write_data_counter += nlb + 1;
        tmp = disk->data_units_written[0];
        disk->data_units_written[0] += (disk->write_data_counter / 1000);
        disk->write_data_counter %= 1000;
        if (tmp > disk->data_units_written[0]) {
            ++disk->data_units_written[1];
        }
    } else if (opcode == NVME_CMD_READ) {
        if (++disk->host_read_commands[0] == 0) {
            ++disk->host_read_commands[1];
        }
        disk->read_data_counter += nlb + 1;
        tmp = disk->data_units_read[0];
        disk->data_units_read[0] += (disk->read_data_counter / 1000);
        disk->read_data_counter %= 1000;
        if (tmp > disk->data_units_read[0]) {
            ++disk->data_units_read[1];
        }
    }
}

/*********************************************************************
    Function     :    nvme_io_command
    Description  :    NVME Read or write cmd processing.

    Return Type  :    uint8_t

    Arguments    :    NVMEState * : Pointer to NVME device State
                      NVMECmd  *  : Pointer to SQ entries
                      NVMECQE *   : Pointer to CQ entries
*********************************************************************/
uint8_t nvme_io_command(NVMEState *n, NVMECmd *sqe, NVMECQE *cqe)
{
    NVME_rw *e = (NVME_rw *)sqe;
    NVMEStatusField *sf = (NVMEStatusField *)&cqe->status;
    uint8_t res = FAIL;
    uint64_t data_size, file_offset;
    uint8_t *mapping_addr;
    uint32_t nvme_blk_sz;
    DiskInfo *disk;
    uint8_t lba_idx;



    sf->sc = NVME_SC_SUCCESS;
    LOG_DBG("%s(): called", __func__);

    disk = &n->disk[e->nsid - 1];
    if ((e->slba + e->nlb) >= disk->idtfy_ns.nsze) {
        LOG_NORM("%s(): LBA out of range", __func__);
        sf->sc = NVME_SC_LBA_RANGE;
        return FAIL;
    } else if ((e->slba + e->nlb) >= disk->idtfy_ns.ncap) {
        LOG_NORM("%s():Capacity Exceeded", __func__);
        sf->sc = NVME_SC_CAP_EXCEEDED;
        return FAIL;
    }

    lba_idx = disk->idtfy_ns.flbas & 0xf;
    if ((e->mptr == 0) &&            /* if NOT supplying separate meta buffer */
    	(disk->idtfy_ns.lbafx[lba_idx].ms != 0) /* if using metadata */
    	//This AND calculation will always yield "0" as the format cannot be 16 but only 0 to 15 (is this a bug?) and will cause sector based access to fail
		//&& ((disk->idtfy_ns.flbas & 0x10) == 0)) /* if using separate buffer */ {
    	&& ((disk->idtfy_ns.flbas & 0xf) == 0)) {
        LOG_ERR("%s(): invalid meta-data for extended lba", __func__);
        sf->sc = NVME_SC_INVALID_FIELD;
        return FAIL;
    }

    object_location obj_loc = {
    		.partition_id = 0,
			.object_id = 0
    };

    /* Spec states that non-zero meta data buffers shall be ignored, i.e. no
     * error reported, when the DW4&5 (MPTR) field is not in use */
    if ((e->mptr != 0) &&                /* if supplying separate meta buffer */
    		(disk->idtfy_ns.lbafx[lba_idx].ms != 0) &&       /* if using metadata */
			((disk->idtfy_ns.flbas & 0x10) == 0)) {   /* if using separate buffer */

    	/* Then go ahead and use the separate meta data buffer */
    	unsigned int ms, meta_offset, meta_size;
    	uint8_t *meta_mapping_addr;

    	ms = disk->idtfy_ns.lbafx[lba_idx].ms;
    	meta_offset = e->slba * ms;

    	//meta_size = (e->nlb + 1) * ms; //metadata size should be set according to the lba format and not according to the block size as it leads to sizes which are bigger than 128 and thus to memory corruption
    	meta_size = ms;
    	meta_mapping_addr = disk->meta_mapping_addr + meta_offset;

    	LOG_DBG("e->mptr (%lu) is: %p\n", e->mptr, (void*)e->mptr);

    	//When writing, we're using the metadata's contents, which contain the partition and object ids, so we know where to write to.
    	//In order to get the metadata's contents, we first read it from the prp, straight to qemu's emulated storage (in memory) and then parse it.
    	//
    	//When reading, we do the exact same, as we want to know what object to read from later on -> we don't want to read the metadata from the "physical" (emulated in memory) storage
    	if (e->opcode == NVME_CMD_READ || e->opcode == NVME_CMD_WRITE) {
    		uint8_t* meta_buf = qemu_mallocz(ms);
    		LOG_DBG("meta_buf is: %p\n", meta_buf);
    		nvme_dma_mem_read(e->mptr, meta_buf, meta_size);
        	parse_metadata(meta_buf, meta_size, &obj_loc);
        	qemu_free(meta_buf);
    	}

     }

    /* Read in the command */
    nvme_blk_sz = NVME_BLOCK_SIZE(disk->idtfy_ns.lbafx[lba_idx].lbads);
    LOG_DBG("NVME Block size: %u", nvme_blk_sz);
    data_size = (e->nlb + 1) * nvme_blk_sz;
    LOG_DBG("e->nlb is: %" PRIu32 " data_size (%lu)\n", e->nlb, data_size);

    if (disk->idtfy_ns.flbas & 0x10) {
        data_size += (disk->idtfy_ns.lbafx[lba_idx].ms * (e->nlb + 1));
    }

    if (n->idtfy_ctrl->mdts && data_size > PAGE_SIZE *
                (1 << (n->idtfy_ctrl->mdts))) {
        LOG_ERR("%s(): data size:%ld exceeds max:%ld", __func__,
            data_size, ((uint64_t)PAGE_SIZE) * (1 << (n->idtfy_ctrl->mdts)));
        sf->sc = NVME_SC_INVALID_FIELD;
        return FAIL;
    }

    file_offset = e->slba * nvme_blk_sz;
    mapping_addr = disk->mapping_addr;

    /* Namespace not ready */
    if (mapping_addr == NULL) {
        LOG_NORM("%s():Namespace not ready", __func__);
        sf->sc = NVME_SC_NS_NOT_READY;
        return FAIL;
    }

    LOG_DBG("mapping_addr is: %p\n", mapping_addr);

    /* Writing/Reading PRP1 */
    LOG_DBG("Writing/Reading PRP1");
    //first prp will always require creating an object
    obj_loc.create_object=true;
    res = do_rw_prp(n, e->prp1, &data_size, &file_offset, mapping_addr,
        e->opcode, obj_loc);
    if (res == FAIL) {
        return FAIL;
    }

    //every prp is of one page size.
    //if the total data is more than 2 pages, we store the first page in prp1 and the rest of the data in a LIST of pages in prp2.
    //in any case, the file offset and mapping address is advanced as we continue writing / reading
    if (data_size > 0) {
    	//need to pass another parameter to do_rw_prp, which specifies that we're updating (appending) the object here(we'll refer to it only in case of object strategy)
    	obj_loc.create_object=false;
        if (data_size <= PAGE_SIZE) {
        	LOG_DBG("Writing/Reading PRP2");
            res = do_rw_prp(n, e->prp2, &data_size, &file_offset, mapping_addr,
                e->opcode, obj_loc);
        } else {
        	LOG_DBG("Writing/Reading do_rw_prp_list!");
            res = do_rw_prp_list(n, sqe, &data_size, &file_offset,
                mapping_addr, obj_loc);
        }
        if (res == FAIL) {
            return FAIL;
        }
    }



    nvme_update_stats(n, disk, e->opcode, e->slba, e->nlb);
    return res;
}

/*********************************************************************
    Function     :    read_dsm_ranges
    Description  :    Read range definition data buffer specified
                      in cmd through prp1 and prp2.

    Return Type  :    uint8_t

    Arguments    :    uint64_t    : PRP1
                      uint64_t    : PRP2
                      uint8_t   * : Pointer to target buffer location
                      uint64_t  * : total data to be copied to target
                                    buffer
*********************************************************************/
static uint8_t read_dsm_ranges(uint64_t range_prp1, uint64_t range_prp2,
    uint8_t *buffer_addr, uint64_t *data_size_p)
{
    uint64_t data_len;

    /* Data Len to be written per page basis */
    data_len = PAGE_SIZE - (range_prp1 % PAGE_SIZE);
    if (data_len > *data_size_p) {
        data_len = *data_size_p;
    }

    nvme_dma_mem_read(range_prp1, buffer_addr, data_len);
    *data_size_p = *data_size_p - data_len;
    if (*data_size_p) {
        buffer_addr = buffer_addr + data_len;
        nvme_dma_mem_read(range_prp2, buffer_addr, *data_size_p);
    }

    return NVME_SC_SUCCESS;
}

/*********************************************************************
    Function     :    dsm_dealloc
    Description  :    De-allocation feature of dataset management cmd.

    Return Type  :    void

    Arguments    :    DiskInfo * : Pointer to NVME device State
                      uint64_t   : Starting LBA
                      uint64_t   : number of LBAs
*********************************************************************/
static void dsm_dealloc(DiskInfo *disk, uint64_t slba, uint64_t nlb)
{
    uint64_t index;

    /* Update the namespace utilization and reset the bit positions */
    for (index = slba; index < (nlb + slba); index++) {
        if ((disk->ns_util[index / 8]) & (1 << (index % 8))) {
            disk->ns_util[(index / 8)] ^= (1 << (index % 8));
            assert(disk->idtfy_ns.nuse > 0);
            disk->idtfy_ns.nuse--;
        }
    }
}

/*********************************************************************
    Function     :    nvme_dsm_command
    Description  :    Dataset management command

    Return Type  :    uint8_t

    Arguments    :    NVMEState * : Pointer to NVME device State
                      NVMECmd   * : Pointer to SQ cmd
                      NVMECQE   * : Pointer to CQ completion entries
*********************************************************************/
uint8_t nvme_dsm_command(NVMEState *n, NVMECmd *sqe, NVMECQE *cqe)
{
    uint16_t nr, i;
    NVMEStatusField *sf = (NVMEStatusField *)&cqe->status;
    DiskInfo *disk;
    uint8_t range_buff[PAGE_SIZE];
    RangeDef *range_defs = (RangeDef *)range_buff;
    uint64_t slba, nlb;
    uint64_t buff_size;

    LOG_NORM("%s(): called", __func__);
    sf->sc = NVME_SC_SUCCESS;

    disk = &n->disk[sqe->nsid - 1];
    nr = (sqe->cdw10 & 0xFF) + 1; /* Convert num ranges to 1-based value */
    buff_size = nr * sizeof(RangeDef);
    assert(buff_size <= PAGE_SIZE);

    read_dsm_ranges(sqe->prp1, sqe->prp2, range_buff, &buff_size);

    LOG_NORM("Processing ranges %d, attribute %d", nr, sqe->cdw11);
    /* Process dsm cmd for attribute deallocate. */
    if (sqe->cdw11 & MASK_AD) {
        for (i = 0; i < nr; i++, range_defs++) {
            slba = range_defs->slba;
            nlb = range_defs->length;
            if ((slba + nlb) > disk->idtfy_ns.ncap) {
                LOG_ERR("Range #%d exceeds namespace capacity(%ld)", (i + 1),
                    disk->idtfy_ns.ncap);
                sf->sc = NVME_SC_LBA_RANGE;
                sf->dnr = 1;
                return FAIL;
            }
            LOG_NORM("DSM deallocate cmd for Range #%d, slba = %ld, nlb = %ld",
                i, slba, nlb);
            dsm_dealloc(disk, slba, nlb);
        }
    }
    return SUCCESS;
}

/*********************************************************************
    Function     :    nvme_command_set
    Description  :    All NVM command set processing

    Return Type  :    uint8_t

    Arguments    :    NVMEState * : Pointer to NVME device State
                      NVMECmd  *  : Pointer to SQ entries
                      NVMECQE *   : Pointer to CQ entries
*********************************************************************/
uint8_t nvme_command_set(NVMEState *n, NVMECmd *sqe, NVMECQE *cqe)
{
    NVMEStatusField *sf = (NVMEStatusField *)&cqe->status;

    /* As of NVMe spec rev 1.0b "All NVM cmds use the CMD.DW1 (NSID) field".
     * Thus all NVM cmd set cmds must check for illegal namespaces up front */
    if (sqe->nsid == 0 || (sqe->nsid > n->idtfy_ctrl->nn)) {
        LOG_NORM("%s(): Invalid nsid:%u", __func__, sqe->nsid);
        sf->sc = NVME_SC_INVALID_NAMESPACE;
        return FAIL;
    }

    if (sqe->opcode == NVME_CMD_READ || (sqe->opcode == NVME_CMD_WRITE)){
        return nvme_io_command(n, sqe, cqe);
    } else if (sqe->opcode == NVME_CMD_DSM) {
        return nvme_dsm_command(n, sqe, cqe);
    } else if (sqe->opcode == NVME_CMD_FLUSH) {
        return NVME_SC_SUCCESS;
    } else {
        LOG_NORM("%s():Wrong IO opcode:\t\t0x%02x", __func__, sqe->opcode);
        sf->sc = NVME_SC_INVALID_OPCODE;
        return FAIL;
    }
}
/*********************************************************************
    Function     :    nvme_create_meta_disk
    Description  :    Creates a meta disk and sets the mapping address

    Return Type  :    int

    Arguments    :    uint32_t *   : Instance number of the nvme device
                      uint32_t *   : Namespace id
                      DiskInfo *   : NVME disk to create storage for
*********************************************************************/
static int nvme_create_meta_disk(uint32_t instance, uint32_t nsid,
    DiskInfo *disk)
{
    uint32_t ms;

    ms = disk->idtfy_ns.lbafx[disk->idtfy_ns.flbas].ms;
    if (ms != 0 && !(disk->idtfy_ns.flbas & 0x10)) {
        char str[64];
        uint64_t blks, msize;

        snprintf(str, sizeof(str), "nvme_meta%d_n%d.img", instance, nsid);
        blks = disk->idtfy_ns.ncap;
        msize = blks * ms;

        disk->mfd = open(str, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
        if (disk->mfd < 0) {
            LOG_ERR("Error while creating the meta-storage");
            return FAIL;
        }
        if (posix_fallocate(disk->mfd, 0, msize) != 0) {
            LOG_ERR("Error while allocating meta-data space for namespace");
            return FAIL;
        }
        disk->meta_mapping_addr = mmap(NULL, msize, PROT_READ | PROT_WRITE,
            MAP_SHARED, disk->mfd, 0);
        if (disk->meta_mapping_addr == NULL) {
            LOG_ERR("Error while opening namespace meta-data: %d", disk->nsid);
            return FAIL;
        }
        disk->meta_mapping_size = msize;
        memset(disk->meta_mapping_addr, 0xff, msize);
    } else {
        disk->meta_mapping_addr = NULL;
        disk->meta_mapping_size = 0;
    }
    return SUCCESS;
}

/*********************************************************************
    Function     :    nvme_create_storage_disk
    Description  :    Creates a NVME Storage Disk and the
                      namespaces within
    Return Type  :    int (0:1 Success:Failure)

    Arguments    :    uint32_t : instance number of the nvme device
                      uint32_t : namespace id
                      DiskInfo * : NVME disk to create storage for
*********************************************************************/
int nvme_create_storage_disk(uint32_t instance, uint32_t nsid, DiskInfo *disk,
    NVMEState *n)
{
    uint32_t blksize, lba_idx;
    uint64_t size, blks;
    char str[PATH_MAX];
#ifdef CONFIG_VSSIM
    strncpy(str, GET_FILE_NAME(), PATH_MAX-1);
    str[PATH_MAX-1] = '\0';
#else
    snprintf(str, sizeof(str), "nvme_disk%d_n%d.img", instance, nsid);
#endif
    disk->nsid = nsid;

    disk->fd = open(str, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    if (disk->fd < 0) {
        LOG_ERR("Error while creating the storage %s", str);
        return FAIL;
    }

    lba_idx = disk->idtfy_ns.flbas & 0xf;
    blks = disk->idtfy_ns.ncap;
    blksize = NVME_BLOCK_SIZE(disk->idtfy_ns.lbafx[lba_idx].lbads);
    size = blks * blksize;
    if (disk->idtfy_ns.flbas & 0x10) {
        /* extended lba */
        size += (blks * disk->idtfy_ns.lbafx[lba_idx].ms);
    }

    if (size == 0) {
        return SUCCESS;
    }

    if (posix_fallocate(disk->fd, 0, size) != 0) {
        LOG_ERR("Error while allocating space for namespace");
        return FAIL;
    }

    disk->mapping_addr = mmap(NULL, size, PROT_READ | PROT_WRITE,
        MAP_SHARED, disk->fd, 0);
    if (disk->mapping_addr == NULL) {
        LOG_ERR("Error while opening namespace: %d", disk->nsid);
        return FAIL;
    }
    disk->mapping_size = size;

    if (nvme_create_meta_disk(instance, nsid, disk) != SUCCESS) {
        return FAIL;
    }

    disk->ns_util = qemu_mallocz((disk->idtfy_ns.nsze + 7) / 8);
    if (disk->ns_util == NULL) {
        LOG_ERR("Error while reallocating the ns_util");
        return FAIL;
    }
    disk->thresh_warn_issued = 0;

    LOG_NORM("created disk storage, mapping_addr:%p size:%lu",
        disk->mapping_addr, disk->mapping_size);

    return SUCCESS;
}

/*********************************************************************
    Function     :    nvme_create_storage_disks
    Description  :    Creates a NVME Storage Disks and the
                      namespaces within
    Return Type  :    int (0:1 Success:Failure)

    Arguments    :    NVMEState * : Pointer to NVME device State
*********************************************************************/
int nvme_create_storage_disks(NVMEState *n)
{
    uint32_t i;
    int ret = SUCCESS;

#ifdef CONFIG_VSSIM
	FTL_INIT();
	if (!osd_init())
		LOG_DBG("Could not init osd !\n");
	#ifdef MONITOR_ON
		INIT_LOG_MANAGER();
	#endif
#endif

    for (i = 0; i < n->num_namespaces; i++) {
        ret = nvme_create_storage_disk(n->instance, i + 1, &n->disk[i], n);
    }

    LOG_NORM("%s():Backing store created for instance %d", __func__,
        n->instance);

    return ret;
}

/*********************************************************************
    Function     :    nvme_close_meta_disk
    Description  :    Deletes NVME meta storage Disk
    Return Type  :    int (0:1 Success:Failure)

    Arguments    :    DiskInfo * : Pointer to NVME disk
*********************************************************************/
static int nvme_close_meta_disk(DiskInfo *disk)
{
    if (disk->meta_mapping_addr != NULL) {
        if (munmap(disk->meta_mapping_addr, disk->meta_mapping_size) < 0) {
            LOG_ERR("Error while closing meta namespace: %d", disk->nsid);
            return FAIL;
        } else {
            disk->meta_mapping_addr = NULL;
            disk->meta_mapping_size = 0;
            if (close(disk->mfd) < 0) {
                LOG_ERR("Unable to close the nvme disk");
                return FAIL;
            }
        }
    }
    return SUCCESS;
}

/*********************************************************************
    Function     :    nvme_close_storage_disk
    Description  :    Deletes NVME Storage Disk
    Return Type  :    int (0:1 Success:Failure)

    Arguments    :    DiskInfo * : Pointer to NVME disk
*********************************************************************/
int nvme_close_storage_disk(DiskInfo *disk)
{
    if (disk->mapping_addr != NULL) {
        if (munmap(disk->mapping_addr, disk->mapping_size) < 0) {
            LOG_ERR("Error while closing namespace: %d", disk->nsid);
            return FAIL;
        } else {
            disk->mapping_addr = NULL;
            disk->mapping_size = 0;
            if (close(disk->fd) < 0) {
                LOG_ERR("Unable to close the nvme disk");
                return FAIL;
            }
        }
        if (disk->ns_util) {
            qemu_free(disk->ns_util);
            disk->ns_util = NULL;
        }
    }
    if (nvme_close_meta_disk(disk) != SUCCESS) {
        return FAIL;
    }
    return SUCCESS;
}

/*********************************************************************
    Function     :    nvme_close_storage_disks
    Description  :    Closes the NVME Storage Disks and the
                      associated namespaces
    Return Type  :    int (0:1 Success:Failure)

    Arguments    :    NVMEState * : Pointer to NVME device State
*********************************************************************/
int nvme_close_storage_disks(NVMEState *n)
{
    uint32_t i;
    int ret = SUCCESS;

    for (i = 0; i < n->num_namespaces; i++) {
        ret = nvme_close_storage_disk(&n->disk[i]);
    }
    
#ifdef CONFIG_VSSIM
    //TODO: nvme_close_storage_disks() function is not called
    //see vl.c for SSD_TERM();
    //SSD_TERM();
#endif

    return ret;
}

