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
#include <sys/mman.h>
#include <assert.h>

#define MASK_AD         0x4
#define MASK_IDW        0x2
#define MASK_IDR        0x1

static uint8_t read_dsm_ranges(uint64_t range_prp1, uint64_t range_prp2,
    uint8_t *buffer_addr, uint64_t *data_size_p);
static void dsm_dealloc(DiskInfo *disk, uint64_t slba, uint64_t nlb);


void nvme_dma_mem_read(target_phys_addr_t addr, uint8_t *buf, int len)
{
//	printf("buf read 0x%016lX\n", (uint64_t)buf);
    cpu_physical_memory_rw(addr, buf, len, 0);
}

void nvme_dma_mem_write(target_phys_addr_t addr, uint8_t *buf, int len)
{
//	printf("buf write 0x%016lX\n", (uint64_t)buf);
    cpu_physical_memory_rw(addr, buf, len, 1);
}

static uint8_t do_rw_prp(NVMEState *n, uint64_t mem_addr, uint64_t *data_size_p,
    uint64_t *file_offset_p, uint8_t *mapping_addr, uint8_t rw)
{
    uint64_t data_len;

    if (*data_size_p == 0) {
        return FAIL;
    }

    /* Data Len to be written per page basis */
    data_len = PAGE_SIZE - (mem_addr % PAGE_SIZE);
    if (data_len > *data_size_p) {
        data_len = *data_size_p;
    }

    LOG_DBG("File offset for read/write:%ld", *file_offset_p);
    LOG_DBG("Length for read/write:%ld (0x%016lX)", data_len, data_len);
    LOG_DBG("Address for read/write:%ld (0x%016lX))", mem_addr, mem_addr);

    switch (rw) {
    case NVME_CMD_READ:
        LOG_DBG("Read cmd called");
        nvme_dma_mem_write(mem_addr, (mapping_addr + *file_offset_p), data_len);
        break;
    case NVME_CMD_WRITE:
        LOG_DBG("Write cmd called");
        nvme_dma_mem_read(mem_addr, (mapping_addr + *file_offset_p), data_len);
        break;
    default:
        LOG_ERR("Error- wrong opcode: %d", rw);
        return FAIL;
    }
    { // DEBUG CODE DUMP
    	unsigned int* start_p = (unsigned int*)(mapping_addr + *file_offset_p);
    	unsigned int* end_p = (unsigned int*)(mapping_addr + *file_offset_p) + data_len / sizeof(unsigned int);
    	int i =0,iall = 0;
    	char msgBuf[512];
    	char* msg = (char*)msgBuf;
    	int gotSomething = 0;
    	printf("---------------------------------------\nDUMP MEMORY START\n0x%016lX - 0x%016lX\n---------------------------------------\n",(uint64_t)start_p,(uint64_t)end_p);

    	while (start_p < end_p){
    		char* asCharArray = (char*)start_p;
    		if (*start_p != 0){
    			gotSomething = 1;
    		}

    		if (i == 0){
    			msg += sprintf(msg, "#%d: 0x%016lX | ", iall, (uint64_t)start_p);
    		}
    		msg += sprintf(msg, "0x%X %c%c%c%c | ", *start_p, asCharArray[0], asCharArray[1], asCharArray[2], asCharArray[3]);
    		if (i == 3) {
    			if (gotSomething){
    				printf("%s\n",msgBuf);
    			}
    			i = 0;
    			gotSomething = 0;
    			msg = (char*)msgBuf;
    		}else{
    			msg += sprintf(msg, " ");
    			i++;
    		}
    		start_p++;
    		iall++;

    	}
    	printf("---------------------------------------\nDUMP MEMORY END\n---------------------------------------\n");
    }

    *file_offset_p = *file_offset_p + data_len;
    *data_size_p = *data_size_p - data_len;
    return NVME_SC_SUCCESS;
}

static uint8_t do_rw_prp_list(NVMEState *n, NVMECmd *command,
    uint64_t *data_size_p, uint64_t *file_offset_p, uint8_t *mapping_addr)
{
    uint64_t prp_list[512], prp_entries;
    uint16_t i = 0;
    uint8_t res = FAIL;
    NVME_rw *cmd = (NVME_rw *)command;

    LOG_DBG("Data Size remaining for read/write:%ld", *data_size_p);

    /* Logic to find the number of PRP Entries */
    prp_entries = (uint64_t) ((*data_size_p + PAGE_SIZE - 1) / PAGE_SIZE);
    nvme_dma_mem_read(cmd->prp2, (uint8_t *)prp_list,
        min(sizeof(prp_list), prp_entries * sizeof(uint64_t)));

    /* Read/Write on PRPList */
    while (*data_size_p != 0) {
        if (i == 511 && *data_size_p > PAGE_SIZE) {
            /* Calculate the actual number of remaining entries */
            prp_entries = (uint64_t) ((*data_size_p + PAGE_SIZE - 1) /
                PAGE_SIZE);
            nvme_dma_mem_read(prp_list[511], (uint8_t *)prp_list,
                min(sizeof(prp_list), prp_entries * sizeof(uint64_t)));
            i = 0;
        }

        res = do_rw_prp(n, prp_list[i], data_size_p,
            file_offset_p, mapping_addr, cmd->opcode);
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
        (disk->idtfy_ns.lbafx[lba_idx].ms != 0) &&       /* if using metadata */
        ((disk->idtfy_ns.flbas & 0x10) == 0)) {   /* if using separate buffer */

        LOG_ERR("%s(): invalid meta-data for extended lba", __func__);
        sf->sc = NVME_SC_INVALID_FIELD;
        return FAIL;
    }

    /* Read in the command */
    nvme_blk_sz = NVME_BLOCK_SIZE(disk->idtfy_ns.lbafx[lba_idx].lbads);
    LOG_DBG("NVME Block size: %u", nvme_blk_sz);
    data_size = (e->nlb + 1) * nvme_blk_sz;

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

    /* Writing/Reading PRP1 */
    LOG_DBG("Writing/Reading PRP1");
    res = do_rw_prp(n, e->prp1, &data_size, &file_offset, mapping_addr,
        e->opcode);
    if (res == FAIL) {
        return FAIL;
    }
    if (data_size > 0) {
        if (data_size <= PAGE_SIZE) {
        	LOG_DBG("Writing/Reading PRP2");
            res = do_rw_prp(n, e->prp2, &data_size, &file_offset, mapping_addr,
                e->opcode);
        } else {
        	LOG_DBG("Writing/Reading do_rw_prp_list!");
            res = do_rw_prp_list(n, sqe, &data_size, &file_offset,
                mapping_addr);
        }
        if (res == FAIL) {
            return FAIL;
        }
    }

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
        meta_size = (e->nlb + 1) * ms;
        meta_mapping_addr = disk->meta_mapping_addr + meta_offset;

        if (e->opcode == NVME_CMD_READ) {
            nvme_dma_mem_write(e->mptr, meta_mapping_addr, meta_size);
        } else if (e->opcode == NVME_CMD_WRITE) {
            nvme_dma_mem_read(e->mptr, meta_mapping_addr, meta_size);
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
    char str[64];

    snprintf(str, sizeof(str), "nvme_disk%d_n%d.img", instance, nsid);
    disk->nsid = nsid;

    disk->fd = open(str, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    if (disk->fd < 0) {
        LOG_ERR("Error while creating the storage");
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
    return ret;
}

