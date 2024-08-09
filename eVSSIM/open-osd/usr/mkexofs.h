/*
 * Copyright (C) 2005, 2006
 * Avishay Traeger (avishay@gmail.com)
 * Copyright (C) 2008, 2009
 * Boaz Harrosh <bharrosh@panasas.com>
 *
 * This file is part of exofs user-mode utilities.
 *
 * exofs is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.  Since it is based on ext2, and the only
 * valid version of GPL for the Linux kernel is version 2, the only valid
 * version of GPL for exofs is version 2.
 *
 * exofs is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with exofs; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef __MKEXOFS_H__
#define __MKEXOFS_H__

#include "common.h"

#include <stdlib.h>
#include <stdio.h>

#define MKFS_ERR(fmt, a...) fprintf(stderr, "mkfs.exofs: " fmt, ##a)
#define MKFS_INFO(fmt, a...) printf("mkfs.exofs: " fmt, ##a)
#define MKFS_PRNT            printf

#ifdef CONFIG_MKEXOFS_DEBUG
#define EXOFS_DBGMSG(fmt, a...) \
	printf("mkfs.exofs @%s:%d: " fmt, __func__, __LINE__, ##a)
#else
#define EXOFS_DBGMSG(fmt, a...) \
	do {} while (0)
#endif

/* u64 has problems with printk this will cast it to unsigned long long */
#define _LLU(x) (unsigned long long)(x)

/* mkexofs.c             */
#define EXOFS_FORMAT_ALL (~0LLU)

int mkexofs_format(struct osd_dev *od, uint64_t format_size_meg,
		   u8 *osdname, unsigned osdname_len);

struct mkexofs_cluster {
	osd_id pid;
	unsigned raid_no;
	unsigned mirrors;
	unsigned stripe_unit;
	unsigned group_width;
	unsigned group_depth;
	unsigned num_ods;
	struct osd_dev *ods[1]; /* variable */
};

int exofs_mkfs(struct mkexofs_cluster *cluster);

#endif /*ndef __MKEXOFS_H__*/
