/*
 * mkexofs.c - make an exofs file system.
 *
 * Copyright (C) 2005, 2006
 * Avishay Traeger (avishay@gmail.com)
 * Copyright (C) 2008, 2009
 * Boaz Harrosh <bharrosh@panasas.com>
 *
 * Copyrights from mke2fs.c:
 *     Copyright (C) 1994, 1995, 1996, 1997, 1998, 1999, 2000, 2001, 2002,
 *     2003, 2004, 2005 by Theodore Ts'o.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "mkexofs.h"
#include <scsi/osd_sense.h>

#ifdef __KERNEL__
#include <linux/random.h>
#else

#include <sys/time.h>

static void get_random_bytes(void *buf, int nbytes)
{
	int left_over = nbytes % sizeof(int);
	int nints = nbytes / sizeof(int);
	int i;

	for (i = 0; i < nints; i++) {
		*((int *)buf) = rand();
		buf += sizeof(int);
	}

	if (left_over) {
		int r = rand();
		memcpy(buf, &r, left_over);
	}
}

static struct timeval current_time(void)
{
struct timeval t;

	if (gettimeofday(&t, NULL))
		t.tv_sec = -1;

	return t;
}

#define CURRENT_TIME current_time()

#endif /* else of def __KERNEL__ */

#ifndef __unused
#    define __unused			__attribute__((unused))
#endif

static void _make_credential(u8 cred_a[OSD_CAP_LEN],
			     const struct osd_obj_id *obj)
{
	osd_sec_init_nosec_doall_caps(cred_a, obj, false, true);
}

static int _check_ok(struct osd_request *or)
{
	struct osd_sense_info osi;
	int ret = osd_req_decode_sense(or, &osi);

	if (unlikely(ret)) { /* translate to Linux codes */
		if (osi.additional_code == scsi_invalid_field_in_cdb) {
			if (osi.cdb_field_offset == OSD_CFO_STARTING_BYTE)
				ret = -EFAULT;
			if (osi.cdb_field_offset == OSD_CFO_OBJECT_ID)
				ret = -ENOENT;
			else
				ret = -EINVAL;
		} else if (osi.additional_code == osd_quota_error)
			ret = -ENOSPC;
		else
			ret = -EIO;
	}

	return ret;
}

static int kick_it(struct osd_request *or, uint8_t *cred,
		   const char *op __unused)
{
	int ret;

	ret = osd_finalize_request(or, 0, cred, NULL);
	if (ret) {
		EXOFS_DBGMSG("Faild to osd_finalize_request() => %d\n", ret);
		return ret;
	}

	osd_execute_request(or);
	ret = _check_ok(or);
	if (unlikely(ret))
		EXOFS_DBGMSG("Execute %s => %d\n", op, ret);

	return ret;
}

/* Format the LUN to the specified size */
int mkexofs_format(struct osd_dev *od, uint64_t format_size_meg, u8 *osdname,
		   unsigned osdname_len)
{
	struct osd_request *or;
	uint8_t cred_a[OSD_CAP_LEN];
	int ret;

	if (!osdname_len || !osdname) {
		MKFS_INFO("Error: osdname must be set\n");
		return -EINVAL;
	}

	if (format_size_meg != EXOFS_FORMAT_ALL) {
		MKFS_INFO("Formatting osdname=[%s] %llu Mgb...",
			  osdname, _LLU(format_size_meg));
	} else {
		MKFS_INFO("Formatting osdname=[%s] all available space...",
			  osdname);
		format_size_meg = 0;
	}

	format_size_meg *= 1024L * 1024L;

	or = osd_start_request(od, GFP_KERNEL);
	if (unlikely(!or))
		return -ENOMEM;

	_make_credential(cred_a, &osd_root_object);
	osd_req_format(or, format_size_meg);

	if (osdname_len) {
		struct osd_attr attr_osdname =
			ATTR_SET(OSD_APAGE_ROOT_INFORMATION,
				 OSD_ATTR_RI_OSD_NAME, osdname_len, osdname);

		osd_req_add_set_attr_list(or, &attr_osdname, 1);
	}

	or->timeout *= 5;
	ret = kick_it(or, cred_a, "format");
	osd_end_request(or);

	MKFS_PRNT(" %s\n", ret ? "FAILED" : "OK");
	return ret;
}

static int create_partition(struct osd_dev *od, osd_id p_id)
{
	struct osd_request *or;
	struct osd_obj_id pid_obj = {p_id, 0};
	uint8_t cred_a[OSD_CAP_LEN];
	int ret;

	_make_credential(cred_a, &pid_obj);

	or = osd_start_request(od, GFP_KERNEL);
	if (unlikely(!or))
		return -ENOMEM;

	osd_req_create_partition(or, p_id);
	ret = kick_it(or, cred_a, "create partition");
	osd_end_request(or);

	return ret ? -EEXIST : 0;
}

static int create(struct osd_dev *od, const struct osd_obj_id *obj)
{
	struct osd_request *or = osd_start_request(od, GFP_KERNEL);
	uint8_t cred_a[OSD_CAP_LEN];
	int ret;

	if (unlikely(!or))
		return -ENOMEM;

	_make_credential(cred_a, obj);
	 /*FIXME osd_req_create_object() */
	osd_req_create_object(or, (struct osd_obj_id *)obj);
	ret = kick_it(or, cred_a, "create");
	osd_end_request(or);

	return ret;
}

/* FIXME: The only thing taken from linux/exportfs/pnfs_osd_xdr.h
 * In the far future when this header will be exported from Kernel, use it!
 */
enum pnfs_osd_raid_algorithm4 {
	PNFS_OSD_RAID_0		= 1,
	PNFS_OSD_RAID_4		= 2,
	PNFS_OSD_RAID_5		= 3,
	PNFS_OSD_RAID_PQ	= 4     /* Reed-Solomon P+Q */
};

static enum pnfs_osd_raid_algorithm4 no2raid(int no)
{
	switch (no) {
	case 0:
		return PNFS_OSD_RAID_0;
	case 4:
		return PNFS_OSD_RAID_4;
	case 5:
		return PNFS_OSD_RAID_5;
	case 6:
		return PNFS_OSD_RAID_PQ;
	default:
		return -1;
	}
}

static int create_write_device_table(struct osd_dev *od,
				     const struct osd_obj_id *obj,
				     const struct mkexofs_cluster *cluster)
{
	struct osd_request *or;
	uint8_t cred_a[OSD_CAP_LEN];
	struct exofs_device_table *edt;
	struct exofs_dt_device_info *dt_dev;
	unsigned numdevs = cluster->num_ods;
	unsigned size = sizeof(*edt) + numdevs * sizeof(*dt_dev);
	unsigned i;
	int ret;

	ret = create(od, obj);
	if (unlikely(ret))
		return ret;

	_make_credential(cred_a, obj);

	or = osd_start_request(od, GFP_KERNEL);
	if (unlikely(!or))
		return -ENOMEM;

	edt = kzalloc(size, GFP_KERNEL);
	if (unlikely(!edt)) {
		ret = -ENOMEM;
		goto out;
	}

	edt->dt_version		= cpu_to_le32(EXOFS_DT_VER);
	edt->dt_num_devices	= cpu_to_le64(numdevs);

	edt->dt_data_map.cb_num_comps		= cpu_to_le32(numdevs);
	edt->dt_data_map.cb_stripe_unit		=
					cpu_to_le64(cluster->stripe_unit);
	edt->dt_data_map.cb_group_width		=
					cpu_to_le32(cluster->group_width);
	edt->dt_data_map.cb_group_depth		=
					cpu_to_le32(cluster->group_depth);
	edt->dt_data_map.cb_mirror_cnt		= cpu_to_le32(cluster->mirrors);
	edt->dt_data_map.cb_raid_algorithm	=
					 cpu_to_le32(no2raid(cluster->raid_no));

	for (i = 0; i < numdevs; i++) {
		const struct osd_dev_info *odi =
			osduld_device_info(cluster->ods[i]);

		dt_dev = &edt->dt_dev_table[i];
		dt_dev->systemid_len = cpu_to_le32(odi->systemid_len);
		memcpy(dt_dev->systemid, odi->systemid, odi->systemid_len);

		/* FIXME support long names*/
		dt_dev->long_name_offset = 0;

		if (unlikely(odi->osdname_len + 1 >= sizeof(dt_dev->osdname))) {
			MKFS_ERR("osdname to long length=%u max=%zu\n",
				 odi->osdname_len + 1,
				 sizeof(dt_dev->osdname));
			ret = -EINVAL;
			goto out;
		}

		/*NOTE: We know lib_osd null-terminates the osdname */
		dt_dev->osdname_len = cpu_to_le32(odi->osdname_len);
		if (odi->osdname_len)
			memcpy(dt_dev->osdname, odi->osdname,
			       odi->osdname_len+1);
	}

	osd_req_write_kern(or, obj, 0, edt, size);
	ret = kick_it(or, cred_a, "write device table");

out:
	osd_end_request(or);
	kfree(edt);
	return ret;
}

static int write_super(struct osd_dev *od, const struct osd_obj_id *obj,
		       unsigned numdevs)
{
	struct osd_request *or = osd_start_request(od, GFP_KERNEL);
	uint8_t cred_a[OSD_CAP_LEN];
	struct exofs_fscb data;
	int ret;

	if (unlikely(!or))
		return -ENOMEM;

	_make_credential(cred_a, obj);

	memset(&data, 0, sizeof(data));
	data.s_nextid = cpu_to_le64(4);
	data.s_magic = cpu_to_le16(EXOFS_SUPER_MAGIC);
	data.s_newfs = 1;
	data.s_numfiles = 0;
	data.s_version = EXOFS_FSCB_VER;
	data.s_dev_table_count = numdevs;

	osd_req_write_kern(or, obj, 0, &data, sizeof(data));
	ret = kick_it(or, cred_a, "write super");
	osd_end_request(or);

	return ret;
}

static int write_rootdir(struct osd_dev *od, const struct osd_obj_id *obj)
{
	struct osd_request *or;
	uint8_t cred_a[OSD_CAP_LEN];
	struct exofs_dir_entry *dir;
	uint64_t off = 0;
	unsigned char *buf = NULL;
	int filetype = EXOFS_FT_DIR << 8;
	int rec_len;
	int done;
	int ret;

	buf = kzalloc(EXOFS_BLKSIZE, GFP_KERNEL);
	if (!buf) {
		MKFS_ERR("ERROR: Failed to allocate memory for root dir.\n");
		return -ENOMEM;
	}
	dir = (struct exofs_dir_entry *)buf;

	/* create entry for '.' */
	dir->name[0] = '.';
	dir->name_len = 1 | filetype;
	dir->inode_no = cpu_to_le64(EXOFS_ROOT_ID - EXOFS_OBJ_OFF);
	dir->rec_len = cpu_to_le16(EXOFS_DIR_REC_LEN(1));
	rec_len = EXOFS_BLKSIZE - EXOFS_DIR_REC_LEN(1);

	/* create entry for '..' */
	dir = (struct exofs_dir_entry *) (buf + le16_to_cpu(dir->rec_len));
	dir->name[0] = '.';
	dir->name[1] = '.';
	dir->name_len = 2 | filetype;
	dir->inode_no = cpu_to_le64(EXOFS_ROOT_ID - EXOFS_OBJ_OFF);
	dir->rec_len = cpu_to_le16(rec_len);
	done = EXOFS_DIR_REC_LEN(1) + le16_to_cpu(dir->rec_len);

	or = osd_start_request(od, GFP_KERNEL);
	if (unlikely(!or))
		return -ENOMEM;

	_make_credential(cred_a, obj);
	osd_req_write_kern(or, obj, off, buf, EXOFS_BLKSIZE);
	ret = kick_it(or, cred_a, "write rootdir");
	osd_end_request(or);

	return ret;
}

static int set_inode(struct osd_dev *od, const struct osd_obj_id *obj,
		     uint16_t i_size, uint16_t mode)
{
	struct osd_request *or;
	uint8_t cred_a[OSD_CAP_LEN];
	struct exofs_fcb inode;
	struct osd_attr attr;
	uint32_t i_generation;
	int ret;

	memset(&inode, 0, sizeof(inode));
	inode.i_mode = cpu_to_le16(mode);
	inode.i_uid = inode.i_gid = 0;
	inode.i_links_count = cpu_to_le16(2);
	inode.i_ctime = inode.i_atime = inode.i_mtime =
				       (signed)cpu_to_le32(CURRENT_TIME.tv_sec);
	inode.i_size = cpu_to_le64(i_size);
/*
	inode.i_size = cpu_to_le64(EXOFS_BLKSIZE);
	if (obj->id != EXOFS_ROOT_ID)
		inode.i_size = cpu_to_le64(64);
*/
	get_random_bytes(&i_generation, sizeof(i_generation));
	inode.i_generation = cpu_to_le32(i_generation);

	or = osd_start_request(od, GFP_KERNEL);
	if (unlikely(!or))
		return -ENOMEM;

	_make_credential(cred_a, obj);
	osd_req_set_attributes(or, obj);

	attr = g_attr_inode_data;
	attr.val_ptr = &inode;
	osd_req_add_set_attr_list(or, &attr, 1);

	ret = kick_it(or, cred_a, "set inode");
	osd_end_request(or);

	return ret;
}

/*
 * This function creates an exofs file system on the specified OSD partition.
 */
static int mkfs_one(struct osd_dev *od, struct mkexofs_cluster *mc)
{
	const struct osd_obj_id obj_root = {mc->pid, EXOFS_ROOT_ID};
	const struct osd_obj_id obj_super = {mc->pid, EXOFS_SUPER_ID};
	const struct osd_obj_id obj_devtable = {mc->pid, EXOFS_DEVTABLE_ID};
	const struct osd_dev_info *odi = osduld_device_info(od);

	int err;

	MKFS_INFO("Adding device osdname-%s {\n", odi->osdname);

	/* Create partition */
	MKFS_INFO("	creating partition...");
	err = create_partition(od, mc->pid);
	if (err)
		goto out;
	MKFS_PRNT(" OK\n");

	/* Create object with known ID for superblock info */
	MKFS_INFO("	creating superblock...");
	err = create(od, &obj_super);
	if (err)
		goto out;
	MKFS_PRNT(" OK\n");

	/* Create root directory object */
	MKFS_INFO("	creating root directory...");
	err = create(od, &obj_root);
	if (err)
		goto out;
	MKFS_PRNT(" OK\n");

	MKFS_INFO("	writing device table...");
	err = create_write_device_table(od, &obj_devtable, mc);
	if (err)
		goto out;
	MKFS_PRNT(" OK\n");

	/* Write superblock */
	MKFS_INFO("	writing superblock...");
	err = write_super(od, &obj_super, mc->num_ods);
	if (err)
		goto out;
	MKFS_PRNT(" OK\n");

	/* Write root directory */
	MKFS_INFO("	writing root directory...");
	err = write_rootdir(od, &obj_root);
	if (err)
		goto out;
	MKFS_PRNT(" OK\n");

	/* Set root partition inode attribute */
	MKFS_INFO("	writing root inode...");
	err = set_inode(od, &obj_root, EXOFS_BLKSIZE, 0040000 | (0777 & ~022));
	if (err)
		goto out;
	MKFS_PRNT(" OK\n");

	MKFS_INFO("}\n");

out:
	if (unlikely(err == -ENOMEM))
		MKFS_ERR("ERROR: Failed to allocate memory\n");

	return err;
}

int exofs_mkfs(struct mkexofs_cluster *cluster)
{
	unsigned i;
	int ret;

	MKFS_INFO("setting up exofs on partition 0x%llx:\n",
		  _LLU(cluster->pid));

	for (i = 0; i < cluster->num_ods; i++) {
		ret = mkfs_one(cluster->ods[i], cluster);
		if (unlikely(ret))
			return ret;
	}

	MKFS_INFO("mkfs complete: enjoy your shiny new exofs!\n");
	return 0;
}

