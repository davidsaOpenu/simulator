/*
 * Processing of SCSI commands addressed to inexistent LUNs.
 *
 * Based on scc.c:
 *
 * Copyright (C) 2007 FUJITA Tomonori <tomof@acm.org>
 * Copyright (C) 2007 Mike Christie <michaelc@cs.wisc.edu>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <linux/fs.h>

#include "list.h"
#include "util.h"
#include "tgtd.h"
#include "target.h"
#include "driver.h"
#include "scsi.h"
#include "tgtadm_error.h"
#include "spc.h"

static int nolun_lu_init(struct scsi_lu *lu)
{
	memset(&lu->attrs, 0x0, sizeof(lu->attrs));
	/*
	 * TODO: use PQ 11b (per. dev. supp but not connected) and dev type 0x0
	 * (SBC-2) instead?
	 */
	lu->attrs.device_type = 0x1f; /* unknown / no device type */
	lu->attrs.qualifier = 0x3; /* peripheral dev. not supp. on this LU */

	return 0;
}

static int nolun_lu_config(struct scsi_lu *lu __attribute__((unused)),
			   char *params __attribute__((unused)))
{
	return TGTADM_NO_LUN;
}

static int nolun_lu_online(struct scsi_lu *lu __attribute__((unused)))
{
	/* TODO: correct return value? most consumers don't check at all ... */
	return -ENOENT;
}

static int nolun_lu_offline(struct scsi_lu *lu __attribute__((unused)))
{
	return 0;
}

static void nolun_lu_exit(struct scsi_lu *lu __attribute__((unused)))
{
	return;
}

static struct device_type_template nolun_template = {
	.type		= TYPE_NO_LUN,
	.lu_init	= nolun_lu_init,
	.lu_config	= nolun_lu_config,
	.lu_online	= nolun_lu_online,
	.lu_offline	= nolun_lu_offline,
	.lu_exit	= nolun_lu_exit,
	.ops		= {
		[0x0 ... 0x11] =  {spc_invalid_lun,},
		{spc_inquiry,}, /* 0x12 */
		[ 0x13 ... 0x9f] =  {spc_invalid_lun},
		{spc_report_luns,}, /* 0xA0 */
		[0xa1 ... 0xff] = {spc_invalid_lun},
	}
};

__attribute__((constructor)) static void nolun_init(void)
{
	device_type_register(&nolun_template);
}
