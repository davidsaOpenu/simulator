/*
 *  C Implementation: osddev.c
 *
 * Description: This file is an abstract wrapper over bsgdev.c that exports
 *              an osd device API.
 *
 * Author: Boaz Harrosh <bharrosh@panasas.com>, (C) 2009
 *
 * Copyright: See COPYING file that comes with this distribution
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
 */

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>

#include <open-osd/libosd.h>
#include <linux/blkdev.h>
#include <linux/bsg.h>
#include <scsi/sg.h>
#include <scsi/scsi_device.h>

#include "osd_debug.h"

struct libosd_dev {
	struct osd_dev od; /* keep this first! (container_of not) */
	struct osd_dev_info odi;
	struct request_queue bsg;
	struct scsi_device scsi_device;
};

int osd_open(const char *osd_path, struct osd_dev **pod)
{
	char bsg_path[_POSIX_PATH_MAX];
	char caps[OSD_CAP_LEN];
	struct libosd_dev *lod;
	int ret;

	*pod = NULL;

	lod = calloc(1, sizeof(*lod));
	if (!lod)
		return ENOMEM;

	ret = osdpath_to_bsgpath(osd_path, bsg_path);
	if (unlikely(ret)) {
		OSD_ERR("Error in osdpath_to_bsgpath(%s) => %d",
			osd_path, ret);
		goto dealloc;
	}

	ret = bsg_open(&lod->bsg, bsg_path);
	if (unlikely(ret)) {
		OSD_ERR("Error bsg_open(%s) => %d", bsg_path, ret);
		goto dealloc;
	}

	lod->scsi_device.request_queue = &lod->bsg;
	osd_dev_init(&lod->od, &lod->scsi_device);
	osd_sec_init_nosec_doall_caps(caps, &osd_root_object, false, true);
	ret = osd_auto_detect_ver(&lod->od, caps, &lod->odi);
	if (unlikely(ret)) {
		OSD_ERR("Error in osd_auto_detect_ver => %d", ret);
		goto bsg_close;
	}

	*pod = &lod->od;
	return 0;

bsg_close:
	osd_dev_fini(&lod->od);
	bsg_close(&lod->bsg);
dealloc:
	free(lod);
	return ret;
}

void osd_close(struct osd_dev *od)
{
	struct libosd_dev *lod = (struct libosd_dev *)od;

	osd_dev_fini(&lod->od);
	bsg_close(&lod->bsg);
	free(lod);
}

const struct osd_dev_info *osduld_device_info(struct osd_dev *od)
{
	struct libosd_dev *lod = (struct libosd_dev *)od;

	return &lod->odi;
}

int osdpath_to_bsgpath(const char *osd_path, char *bsg_path)
{
	DIR *device_dir;
	struct dirent *entry;
	char sys_path[_POSIX_PATH_MAX];

	*bsg_path = 0;

	sprintf(sys_path, "/sys/class/scsi_osd/%s/device/",
		osd_path + sizeof("/dev/") - 1);

	device_dir = opendir(sys_path);
	if (!device_dir)
		return ENODEV;

	while ((entry = readdir(device_dir))) {
		if (0 == strncmp(entry->d_name, "bsg", 3)) {
			if (0 == strcmp(entry->d_name, "bsg")) {
				/* device is a single file in the bsg/ subdir */
				DIR *bsg_dir;
				struct dirent *entry2;

				strcat(sys_path, "bsg/");
				bsg_dir = opendir(sys_path);
				if (!bsg_dir)
					return ENODEV;

				while ((entry2 = readdir(bsg_dir))) {
					/* ignore the . and .. entries */
					if (entry2->d_name[0] == '.')
						continue;
					sprintf(bsg_path,
						"/dev/bsg/%s", entry2->d_name);
					return 0;
				}
			} else {
				/* name of device is of the form: bsg:X:X:X:X */
				sprintf(bsg_path,
					"/dev/bsg/%s", entry->d_name + 4);
				return 0;
			}
		}
	}

	return ENODEV;
}
