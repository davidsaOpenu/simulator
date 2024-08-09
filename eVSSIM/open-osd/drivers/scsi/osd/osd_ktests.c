/*
 * osd_ktests.c - An osd_initiator library in-kernel test suite
 *              called by the osd_uld module
 *
 * Copyright (C) 2008 Panasas Inc.  All rights reserved.
 *
 * Authors:
 *   Boaz Harrosh <bharrosh@panasas.com>
 *   Benny Halevy <bhalevy@panasas.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. Neither the name of the Panasas company nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#include <asm/unaligned.h>
#include <scsi/scsi_device.h>

#include <scsi/osd_initiator.h>
#include <scsi/osd_sec.h>
#include <scsi/osd_attributes.h>

#include "osd_ktests.h"
#include "osd_debug.h"

#ifndef __unused
#    define __unused			__attribute__((unused))
#endif

enum {
	K = 1024,
	M = 1024 * K,
	G = 1024 * M,
};

const u64 format_total_capacity = 128 * M;
const osd_id first_par_id = 0x17171717L;
const osd_id first_obj_id = 0x18181818L;
const unsigned BUFF_SIZE = 4 * K;

const int num_partitions = 1;
const int num_objects = 2; /* per partition */
const int num_sg_entries = 4;

static struct osd_request *_start_request(struct osd_dev *od,
					  const char *func, int line)
{
	struct osd_request *or = osd_start_request(od, GFP_KERNEL);

	if (unlikely(!or)) {
		OSD_ERR("Error @%s:%d: osd_start_request\n", func, line);
		return NULL;
	}
	return or;
}

static int _exec_only(struct osd_request *or, const struct osd_obj_id *obj,
		 u8 *caps, const char *msg)
{
	int ret;
	struct osd_sense_info osi;

	osd_sec_init_nosec_doall_caps(caps, obj, false, true);
	ret = osd_finalize_request(or, 0, caps, NULL);
	if (ret)
		goto out;

	osd_execute_request(or);
	ret = osd_req_decode_sense(or, &osi);

out:
	if (msg) {
		if (!ret)
			OSD_INFO("%s\n", msg);
		else
			OSD_ERR("Error executing %s => %d\n", msg, ret);
	}

	return ret;
}

static int _exec(struct osd_request *or, const struct osd_obj_id *obj,
		 u8 *caps, const char *msg)
{
	int ret = _exec_only(or, obj, caps, msg);

	osd_end_request(or);
	return ret;
}

static int ktest_format(struct osd_dev *osd_dev)
{
	struct osd_request *or;
	u8 caps[OSD_CAP_LEN];
	int ret;

	or = _start_request(osd_dev, __func__, __LINE__);
	if (!or)
		return -ENOMEM;

	or->timeout *= 10;
	osd_req_format(or, format_total_capacity);
	ret = _exec(or, &osd_root_object, caps, "format");
	return ret;
}

static int ktest_creat_par(struct osd_dev *osd_dev)
{
	struct osd_request *or;
	u8 caps[OSD_CAP_LEN];
	int ret;
	int p;

	for (p = 0; p < num_partitions; p++) {
		struct osd_obj_id par = {
			.partition = first_par_id + p,
			.id = 0
		};

		or = _start_request(osd_dev, __func__, __LINE__);
		if (!or)
			return -ENOMEM;

		osd_req_create_partition(or, par.partition);
		ret = _exec(or, &par, caps, "create_partition");
		if (ret)
			return ret;
	}

	return 0;
}

static int ktest_creat_obj(struct osd_dev *osd_dev)
{
	struct osd_request *or;
	u8 caps[OSD_CAP_LEN];
	int ret;
	int p, o;

	for (p = 0; p < num_partitions; p++)
		for (o = 0; o < num_objects; o++) {
			struct osd_obj_id obj = {
				.partition = first_par_id + p,
				.id = first_obj_id + o
			};

			or = _start_request(osd_dev, __func__, __LINE__);
			if (!or)
				return -ENOMEM;

			osd_req_create_object(or, &obj);
			ret = _exec(or, &obj, caps, "create_object");
			if (ret)
				return ret;
		}

	return 0;
}

static int ktest_write_obj(struct osd_dev *osd_dev, void *write_buff,
			   bool will_fail)
{
	struct osd_request *or;
	u8 caps[OSD_CAP_LEN];
	int ret;
	int p, o; u64 offset = 0;

	for (p = 0; p < num_partitions; p++)
		for (o = 0; o < num_objects; o++) {
			struct osd_obj_id obj = {
				.partition = first_par_id + p,
				.id = first_obj_id + o
			};

			or = _start_request(osd_dev, __func__, __LINE__);
			if (!or)
				return -ENOMEM;

			ret = osd_req_write_kern(or, &obj, offset,
					   write_buff, BUFF_SIZE);
			if (ret) {
				OSD_ERR("!!! Failed osd_req_write_kern\n");
				return ret;
			}

			ret = _exec(or, &obj, caps, will_fail ? NULL : "write");
			if (ret)
				return ret;

			offset += BUFF_SIZE;
		}

	return 0;
}

static int ktest_read_obj(struct osd_dev *osd_dev, void *write_buff, void *read_buff)
{
	struct osd_request *or;
	u8 caps[OSD_CAP_LEN];
	int ret;
	int p, o; u64 offset = 0;

	for (p = 0; p < num_partitions; p++)
		for (o = 0; o < num_objects; o++) {
			struct osd_obj_id obj = {
				.partition = first_par_id + p,
				.id = first_obj_id + o
			};

			or = _start_request(osd_dev, __func__, __LINE__);
			if (!or)
				return -ENOMEM;

			ret = osd_req_read_kern(or, &obj, offset,
					  read_buff, BUFF_SIZE);
			if (ret) {
				OSD_ERR("!!! Failed osd_req_read_kern\n");
				return ret;
			}

			ret = _exec(or, &obj, caps, "read");
			if (ret)
				return ret;

			if (memcmp(read_buff, write_buff, BUFF_SIZE))
				OSD_ERR("!!! Read did not compare\n");
			offset += BUFF_SIZE;
		}

	return 0;
}

static int ktest_write_sg_obj(struct osd_dev *osd_dev, void *write_buff)
{
	struct osd_request *or;
	u8 caps[OSD_CAP_LEN];
	int ret;
	int p, o, s; u64 offset = 0;
	struct osd_sg_entry sglist[num_sg_entries];
	void *buff[num_sg_entries];
	u64 segsize = BUFF_SIZE/num_sg_entries;

	/* divide the write_buff into num_sg_entries segments and
	   write the segments in reverse order to the object */
	for (s = 0; s < num_sg_entries; s++) {
		buff[s] = &(((uint8_t *)write_buff)[offset]);
		offset += segsize;
		sglist[s].offset = BUFF_SIZE-offset;
		sglist[s].len = segsize;
	}

	for (p = 0; p < num_partitions; p++)
		for (o = 0; o < num_objects; o++) {
			struct osd_obj_id obj = {
				.partition = first_par_id + p,
				.id = first_obj_id + o
			};

			or = _start_request(osd_dev, __func__, __LINE__);
			if (!or)
				return -ENOMEM;
			ret = osd_req_write_sg_kern(or, &obj, buff, sglist,
						    num_sg_entries);
			if (ret) {
				OSD_ERR("!!! Failed osd_req_write_sg_kern\n");
				return ret;
			}

			ret = _exec(or, &obj, caps, "write_sg");
			if (ret)
				return ret;
		}

	return 0;
}

static int ktest_read_sg_obj(struct osd_dev *osd_dev,
			     void *write_buff, void *read_buff)
{
	struct osd_request *or;
	u8 caps[OSD_CAP_LEN];
	int ret;
	int p, o, i, s; u64 offset = 0;
	struct osd_sg_entry sglist[num_sg_entries];
	void *buff[num_sg_entries];
	u64 segsize = BUFF_SIZE/num_sg_entries;

	/* divide the read_buff into num_sg_entries segments and
	   read the segments in reverse order from the object */
	for (s = 0; s < num_sg_entries; s++) {
		buff[s] = &(((uint8_t *)read_buff)[offset]);
		offset += segsize;
		sglist[s].offset = BUFF_SIZE-offset;
		sglist[s].len = segsize;
	}

	for (p = 0; p < num_partitions; p++)
		for (o = 0; o < num_objects; o++) {
			struct osd_obj_id obj = {
				.partition = first_par_id + p,
				.id = first_obj_id + o
			};

			/* first read it normally to see that the
			   writes got scattered */
			or = _start_request(osd_dev, __func__, __LINE__);
			if (!or)
				return -ENOMEM;

			ret = osd_req_read_kern(or, &obj, 0,
					  read_buff, BUFF_SIZE);
			if (ret) {
				OSD_ERR("!!! Failed osd_req_read_kern\n");
				return ret;
			}

			ret = _exec(or, &obj, caps, "read");
			if (ret)
				return ret;

			for (i = 0; i < num_sg_entries; i++) {
				uint8_t *rbuff =
					&(((uint8_t *)read_buff)[i * segsize]);
				uint8_t *wbuff =
					&(((uint8_t *)write_buff)[
					       (num_sg_entries-i-1) * segsize]);
				if (memcmp(rbuff, wbuff, segsize))
					OSD_ERR("!!! Read did not compare\n");
			}

			/* now read it with the scatter gather list */
			or = _start_request(osd_dev, __func__, __LINE__);
			if (!or)
				return -ENOMEM;

			ret = osd_req_read_sg_kern(or, &obj, buff, sglist,
						   num_sg_entries);
			if (ret) {
				OSD_ERR("!!! Failed osd_req_read_sg_kern\n");
				return ret;
			}

			ret = _exec(or, &obj, caps, "read_sg");
			if (ret)
				return ret;

			if (memcmp(read_buff, write_buff, BUFF_SIZE))
				OSD_ERR("!!! Read SG did not compare\n");
		}

	return 0;
}

static int ktest_remove_obj(struct osd_dev *osd_dev)
{
	struct osd_request *or;
	u8 caps[OSD_CAP_LEN];
	int ret;
	int p, o;

	for (p = 0; p < num_partitions; p++)
		for (o = 0; o < num_objects; o++) {
			struct osd_obj_id obj = {
				.partition = first_par_id + p,
				.id = first_obj_id + o
			};

			or = _start_request(osd_dev, __func__, __LINE__);
			if (!or)
				return -ENOMEM;

			osd_req_remove_object(or, &obj);
			ret = _exec(or, &obj, caps, "remove_object");
			if (ret)
				return ret;
		}

	return 0;
}

static int ktest_remove_par(struct osd_dev *osd_dev)
{
	struct osd_request *or;
	u8 caps[OSD_CAP_LEN];
	int ret;
	int p;

	for (p = 0; p < num_partitions; p++) {
		struct osd_obj_id par = {
			.partition = first_par_id + p,
			.id = 0
		};

		or = _start_request(osd_dev, __func__, __LINE__);
		if (!or)
			return -ENOMEM;

		osd_req_remove_partition(or, par.partition);
		ret = _exec(or, &par, caps, "remove_partition");
		if (ret)
			return ret;
	}

	return 0;
}

static int ktest_write_read_attr(struct osd_dev *osd_dev, void *buff,
	bool doread, bool doset, bool doget)
{
	struct osd_request *or;
	u8 caps[OSD_CAP_LEN];
	int ret;
	const char *domsg;
	/* set attrs */
	static char name[] = "ktest_write_read_attr";
	__be64 max_len = cpu_to_be64(0x80000000L);
	struct osd_obj_id obj = {
		.partition = first_par_id,
		.id = first_obj_id,
	};
	struct osd_attr set_attrs[] = {
		ATTR_SET(OSD_APAGE_OBJECT_QUOTAS, OSD_ATTR_OQ_MAXIMUM_LENGTH,
			sizeof(max_len), &max_len),
		ATTR_SET(OSD_APAGE_OBJECT_INFORMATION, OSD_ATTR_OI_USERNAME,
			sizeof(name), name),
	};
	struct osd_attr get_attrs[] = {
		ATTR_DEF(OSD_APAGE_OBJECT_INFORMATION,
			OSD_ATTR_OI_USED_CAPACITY, sizeof(__be64)),
		ATTR_DEF(OSD_APAGE_OBJECT_INFORMATION,
			OSD_ATTR_OI_LOGICAL_LENGTH, sizeof(__be64)),
	};

	or = _start_request(osd_dev, __func__, __LINE__);
	if (!or)
		return -ENOMEM;

	if (doread) {
		ret = osd_req_read_kern(or, &obj, 0, buff, BUFF_SIZE);
		domsg = "Read-with-attr";
	} else {
		ret = osd_req_write_kern(or, &obj, 0, buff, BUFF_SIZE);
		domsg = "Write-with-attr";
	}

	if (ret) {
		OSD_ERR("!!! Failed on osd_req_read/write_kern\n");
		goto out;
	}

	if (doset)
		osd_req_add_set_attr_list(or, set_attrs, 2);
	if (doget)
		osd_req_add_get_attr_list(or, get_attrs, 2);

	ret = _exec_only(or, &obj, caps, NULL);
	if (!ret && doget) {
		void *iter = NULL, *pFirst, *pSec;
		int nelem = 2;
		u64 capacity_len = ~0;
		u64 logical_len = ~0;

		osd_req_decode_get_attr_list(or, get_attrs, &nelem, &iter);

		/*FIXME: Std does not guaranty order of return attrs */
		pFirst = get_attrs[0].val_ptr;
		if (pFirst)
			capacity_len = get_unaligned_be64(pFirst);
		else
			OSD_ERR("failed to read capacity_used\n");
		pSec = get_attrs[1].val_ptr;
		if (pSec)
			logical_len = get_unaligned_be64(pSec);
		else
			OSD_ERR("failed to read logical_length\n");
		OSD_INFO("%s capacity=%llu len=%llu\n",
			domsg, _LLU(capacity_len), _LLU(logical_len));
	}

out:
	osd_end_request(or);
	if (ret) {
		OSD_ERR("!!! Error executing %s => %d doset=%d doget=%d\n",
			domsg, ret, doset, doget);
		return ret;
	}

	return 0;
}

static int ktest_clocks(struct osd_dev *osd_dev, void *buff, bool do_user_attr)
{
	struct osd_obj_id obj = {
		.partition = first_par_id,
		.id = first_obj_id,
	};
	struct osd_attr set_user_attr[] = {
		ATTR_SET(OSD_APAGE_APP_DEFINED_FIRST, 1, sizeof(obj), &obj),
	};
	struct osd_attr get_clocks[] = {
		ATTR_DEF(OSD_APAGE_OBJECT_TIMESTAMP,
			OSD_ATTR_OT_DATA_MODIFIED_TIME, sizeof(__be64)),
		ATTR_DEF(OSD_APAGE_OBJECT_TIMESTAMP,
			OSD_ATTR_OT_ATTRIBUTES_MODIFIED_TIME, sizeof(__be64)),
	};
	struct osd_request *or;
	u8 caps[OSD_CAP_LEN];
	int ret;

	// write data and/or user_attar
	or = _start_request(osd_dev, __func__, __LINE__);
	if (!or)
		return -ENOMEM;

	if (buff) {
		ret = osd_req_write_kern(or, &obj, 0,buff, BUFF_SIZE);
		if (ret) {
			OSD_ERR("!!! Failed osd_req_write_kern\n");
			goto out;
		}
	} else {
		osd_req_set_attributes(or, &obj);
	}

	if (do_user_attr)
		osd_req_add_set_attr_list(or, set_user_attr, 1);

	osd_req_add_get_attr_list(or, get_clocks, 2);

	ret = _exec_only(or, &obj, caps, "test_clocks");
	if (ret)
		goto out;

	// decode print two clocks
	if (!ret) {
		void *iter = NULL, *pFirst, *pSec;
		int nelem = 2;
		struct timespec data_time = {-1, -1}, attr_time = {-1, -1};

		osd_req_decode_get_attr_list(or, get_clocks, &nelem, &iter);

		pFirst = get_clocks[0].val_ptr;
		if (pFirst)
			osd_otime_2_utime(pFirst, &data_time);
		else
			OSD_ERR("failed to read data_time\n");
		pSec = get_clocks[1].val_ptr;
		if (pSec)
			osd_otime_2_utime(pSec, &attr_time);
		else
			OSD_ERR("failed to read logical_length\n");
		OSD_INFO("Clocks data_time=%ld:%ld attr_time=%ld:%ld\n",
			 data_time.tv_sec, data_time.tv_nsec,
			 attr_time.tv_sec, attr_time.tv_nsec);
	}

	// done
out:
	osd_end_request(or);
	return ret;
}

int do_test_17(struct osd_dev *od, unsigned cmd __unused,
		unsigned long arg __unused)
{
	void *write_buff = NULL;
	void *read_buff = NULL;
	int ret;
	unsigned i;

/* osd_format */
	ret = ktest_format(od);
	if (ret)
		goto dev_fini;

/* create some partition */
	ret = ktest_creat_par(od);
	if (ret)
		goto dev_fini;
/* list partition see if they're all there */
/* create some objects on some partitions */
	ret = ktest_creat_obj(od);
	if (ret)
		goto dev_fini;

/* Alloc some buffers and bios */
/*	write_buff = kmalloc(BUFF_SIZE, or->alloc_flags);*/
/*	read_buff = kmalloc(BUFF_SIZE, or->alloc_flags);*/
	write_buff = (void *)__get_free_page(GFP_KERNEL);
	read_buff = (void *)__get_free_page(GFP_KERNEL);
	if (!write_buff || !read_buff) {
		OSD_ERR("!!! Failed to allocate memory for test\n");
		ret = -ENOMEM;
		goto dev_fini;
	}
	for (i = 0; i < BUFF_SIZE / 4; i++)
		((int *)write_buff)[i] = i;
	OSD_DEBUG("allocate buffers\n");

/* write to objects */
	ret = ktest_write_obj(od, write_buff, false);
	if (ret)
		goto dev_fini;

/* read from objects and compare to write */
	ret = ktest_read_obj(od, write_buff, read_buff);
	if (ret)
		goto dev_fini;

/* write sg to objects */
	ret = ktest_write_sg_obj(od, write_buff);
	if (ret)
		goto dev_fini;

/* read sg from objects */
	ret = ktest_read_sg_obj(od, write_buff, read_buff);
	if (ret)
		goto dev_fini;

/* List all objects */

/* Write with get_attr */
	ret = ktest_write_read_attr(od, write_buff, false, false, true);
	if (ret)
		goto dev_fini;

/* Write with set_attr */
	ret = ktest_write_read_attr(od, write_buff, false, true, false);
	if (ret)
		goto dev_fini;

/* Write with set_attr + get_attr */
	ret = ktest_write_read_attr(od, write_buff, false, true, true);
	if (ret)
		goto dev_fini;

/* Read with set_attr */
	ret = ktest_write_read_attr(od, write_buff, true, true, false);
	if (ret)
		goto dev_fini;

/* Read with get_attr */
	ret = ktest_write_read_attr(od, write_buff, true, false, true);
	if (ret)
		goto dev_fini;

/* Read with get_attr + set_attr */
	ret = ktest_write_read_attr(od, write_buff, true, true, true);
	if (ret)
		goto dev_fini;

/* Print clocks after data_write & attr_set */
	ret = ktest_clocks(od, write_buff, true);
	if (ret)
		goto dev_fini;

/* Print clocks after data_write only */
	ret = ktest_clocks(od, write_buff, false);
	if (ret)
		goto dev_fini;

/* Print clocks after attr_set only */
	ret = ktest_clocks(od, NULL, true);
	if (ret)
		goto dev_fini;

/* Print clocks after truncate */
/*	ret = ktest_clocks_truncate(od);
	if (ret)
		goto dev_fini;
*/
/* remove objects */
	ret = ktest_remove_obj(od);
	if (ret)
		goto dev_fini;

/* remove partitions */
	ret = ktest_remove_par(od);
	if (ret)
		goto dev_fini;

/* Error to test sense handling */
	ret = ktest_write_obj(od, write_buff, true);
	if (!ret) {
		OSD_ERR("Error was expected written to none existing object\n");
		ret = -EIO;
		goto dev_fini;
	} else
		ret =  0; /* good this test should fail */

/* good and done */
	OSD_INFO("test17: All good and done\n");
dev_fini:
	if (read_buff)
		free_page((ulong)read_buff);
	if (write_buff)
		free_page((ulong)write_buff);

	return ret;
}

#ifdef __KERNEL__
static const char *osd_ktests_version = "open-osd osd_ktests 0.1.0";

MODULE_AUTHOR("Boaz Harrosh <bharrosh@panasas.com>");
MODULE_DESCRIPTION("open-osd Kernel tests Driver osd_ktests.ko");
MODULE_LICENSE("GPL");

static int __init osd_uld_init(void)
{
	OSD_INFO("LOADED %s\n", osd_ktests_version);
	osduld_register_test(OSD_TEST_ALL, do_test_17);
	return 0;
}

static void __exit osd_uld_exit(void)
{
	osduld_unregister_test(OSD_TEST_ALL);
	OSD_INFO("UNLOADED %s\n", osd_ktests_version);
}

module_init(osd_uld_init);
module_exit(osd_uld_exit);

#endif
