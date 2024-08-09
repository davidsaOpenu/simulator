/*
 * osd_test.c - A user-mode program that calls into the osd ULD
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
 */

#include <dirent.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <open-osd/libosd.h>
#include "mkexofs.h"

static void usage(void)
{
	static char msg[] = {
	"usage: mkfs.exofs --raid=# --pid=pid_no --dev=/dev/osdX [options]...\n"
	"       repeated for each device\n"
	"[Global options]\n"
	"--raid=raid_num -r raid_num (optional)\n"
	"        raid_num is the raid engine to use. It can be: 0, 4, 5, 6\n"
	"        Default: 0\n"
	"\n"
	"--pid=pid_no -p pid_no\n"
	"        pid_no is the partition number that will contain the new\n"
	"        exofs filesystem. Minimum is %d (0x%x). pid_no can\n"
	"        start with 0x to denote an hex number\n"
	"        Note: Same pid is used on all devices\n"
	"\n"
	"--mirrors=num_of_mirrors -m num_of_mirrors (optional)\n"
	"        num_of_mirrors is the additional mirror device count to use.\n"
	"        Note: \"device count\" % (num_of_mirrors+1) must be Zero\n"
	"        Default: 0\n"
	"\n"
	"--stripe_pages=stripe_pages -s stripe_pages (optional)\n"
	"        the 4k count in a stripe_unit.\n"
	"        stripe_unit is the amount to write/read to a device before\n"
	"	 moving to the next device\n"
	"	 stripe_pages * 4096 == stripe_unit"
	"        Default: 1\n"
	"\n"
	"--group_width=group_width -g group_width (optional)\n"
	"        Number of devices in a stripe. (Without data integrity)\n"
	"        == 0: No groups are used and the devices-in-a-stripe is the\n"
	"	       number-of-devices divided by mirrors\n"
	"	 != 0: Number of devices-in-a-stripe\n"
	"        Note: {number-of-devices / mirrors} need not be divisible by\n"
	"        group_width. It just means that the reminder of devices at\n"
	"        the end will not be used by this particular file (object)\n"
	"        but they will be used by other objects, as each object sees\n"
	"        the device table as a cyclic list\n"
	"        Default: 0\n"
	"--group_depth=group_depth -t group_depth (optional)\n"
	"        Number of times a stripe group is used before advancing to\n"
	"        the next group of devices\n"
	"        Must not be Zero if group_width is not zero\n"
	"        Default: 0\n"
	"\n"
	"[Per Device options]\n"
	"--dev=/dev/osdX\n"
	"        /dev/osdX is the osd LUN (char-dev) to add to the exofs\n"
	"        devices table. This option is repeated for each device,\n"
	"        following by an optional --format and --osdname for each\n"
	"        device."
	"\n"
	"--format[=format_size_meg] (optional)\n"
	"        First format the OSD LUN (device) before preparing the\n"
	"        filesystem. format_size_meg * 2^20 is the size in bytes\n"
	"        to use with the OSD_FORMAT command (0x for hex). The size\n"
	"        is optional, if not specified, or is bigger then, all\n"
	"        available space will be used\n"
	"\n"
	"--osdname=user_set_osd_name\n"
	"        Set the root partition's osd_name attribute to this value.\n"
	"        for unique identification of the osd device.\n"
	"        Usually this is a value returned from the uuidgen command.\n"
	"        only applies if used with --format\n"
	"        This option is mandatory if --format is used. If a given\n"
	"        device is not specified for format and it has a null osdname\n"
	"        it can not be used.\n"
	"        Careful: osdnames must be network unique\n"
	"\n"
	"Description: An exofs filesystem resides in one partition number on\n"
	"one or more devices. Outlaid in the specified raid arrangement.\n"
	"For any raid there can be also extra mirrors. The mirror devices are\n"
	"ordered immediately after the first device in the stripe.\n"
	"The mkexofs utility operates in two passes. First pass arranges all\n"
	"devices in the proper raid+mirrors arrangement, formatting any\n"
	"specified devices. Second pass will prepare all FS structures\n"
	"necessary for an empty filesystem. Note that at this stage all\n"
	"devices are identical, as most meta-data is mirrored in exofs. Any\n"
	"of the devices specified can be passed to the mount command\n"
	};

	printf(msg, EXOFS_MIN_PID, EXOFS_MIN_PID);
}

struct _one_dev {
	char *path;
	u64 format_size_meg;
	char *osdname;
};

static int _format(struct _one_dev *dev)
{
	struct osd_dev *od;
	int ret;

	ret = osd_open(dev->path, &od);
	if (ret) {
		printf("exofs_mkfs: Error! Could not open [%s]\n", dev->path);
		return ret;
	}

	BUG_ON(!od);

	if (!dev->osdname) {
		printf("exofs_mkfs: Error! --format must also use --osdname\n"
			"Please see \"exofs_mkfs --help\"\n");
		return EINVAL;
	}

	ret = mkexofs_format(od, dev->format_size_meg, (u8 *)dev->osdname,
			     strlen(dev->osdname));
	if (ret) {
		/* exofs_format is a kernel API it returns negative errors */
		ret = -ret;
		printf("exofs_mkfs exofs_format returned %d: %s\n",
			ret, strerror(ret));
	}

	osd_close(od);

	dev->format_size_meg = 0;
	free(dev->osdname);
	dev->osdname = NULL;
	/* dev->path is passed to 2nd-pass, don't free */
	dev->path = NULL;

	return ret;
}

static int check_supported_params(struct mkexofs_cluster *c_header)
{
	switch (c_header->raid_no) {
	case 0:
	case 5:
	case 6:
		break;
	case 4:
	default:
		printf("ERROR: --raid==4 is currently not supported\n");
		return EINVAL;
	}

	if (0 != (c_header->num_ods % (c_header->mirrors+1))) {
		printf("ERROR: Number_of_devices(%u) must be Multiple of"
		       "(--mirrors(%u) + 1)\n",
		       c_header->num_ods, c_header->mirrors);
		return EINVAL;
	}

	unsigned stripe_count;
	if (c_header->group_width)
		stripe_count = c_header->group_width;
	else
		stripe_count = c_header->num_ods / (c_header->mirrors+1);
	u64 stripe_length = (u64)stripe_count * c_header->stripe_unit;

	if ( !stripe_length || (stripe_length >= (1ULL << 32))) {
		printf("ERROR: stripe_unit * stripe_count must be less then"
		       "32bit! stripe_unit=0x%x stripe_count=0x%x\n",
			c_header->stripe_unit, stripe_count);
		return EINVAL;
	}
	return 0;
}

static int _mkfs(char **pathes, struct mkexofs_cluster *c_header)
{
	struct mkexofs_cluster *cluster;
	unsigned num_devs = c_header->num_ods;
	unsigned i;
	int ret;

	ret = check_supported_params(c_header);
	if (ret)
		return ret;

	cluster = malloc(sizeof(*cluster) + sizeof(cluster->ods[0]) * num_devs);
	if (!cluster)
		return ENOMEM;

	*cluster = *c_header;
	cluster->num_ods = 0;
	for (i = 0; i < num_devs; i++) {
		struct osd_dev *od;

		ret = osd_open(pathes[i], &od);
		if (ret)
			goto failed;

		cluster->ods[i] = od;
		cluster->num_ods++;
	}

	ret = exofs_mkfs(cluster);

failed:
	while (cluster->num_ods--)
		osd_close(cluster->ods[cluster->num_ods]);

	if (ret) {
		/* exofs_mkfs is a kernel API it returns negative errors */
		ret = -ret;
		printf("exofs_mkfs --pid=0x%llx returned %d: %s\n",
			_LLU(cluster->pid), ret, strerror(ret));
	}

	return ret;
}

int main(int argc, char *argv[])
{
	struct option opt[] = {
		/* Global */
		{.name = "pid", .has_arg = 1, .flag = NULL, .val = 'p'} ,
		{.name = "raid", .has_arg = 1, .flag = NULL, .val = 'r'} ,
		{.name = "mirrors", .has_arg = 1, .flag = NULL, .val = 'm'} ,
		{.name = "stripe_pages", .has_arg = 1, .flag = NULL,
								.val = 's'} ,
		{.name = "group_width", .has_arg = 1, .flag = NULL,
								.val = 'g'} ,
		{.name = "group_depth", .has_arg = 1, .flag = NULL,
								.val = 't'} ,

		/* Per Device */
		{.name = "dev", .has_arg = 1, .flag = NULL, .val = 'd'} ,
		{.name = "format", .has_arg = 2, .flag = NULL, .val = 'f'} ,
		{.name = "osdname", .has_arg = 1, .flag = NULL, .val = 'o'} ,
		{.name = 0, .has_arg = 0, .flag = 0, .val = 0} ,
	};
	struct mkexofs_cluster c_header = {
		.pid = 0, .raid_no = 0, .mirrors = 0, .num_ods = 0,
		.stripe_unit = EXOFS_BLKSIZE,
	};
	struct _one_dev dev = {.path = NULL};
	unsigned max_devs = 0;
	char **devs = NULL;
	char op;
	int ret;

	while (-1 != (op = getopt_long(argc, argv,
				       "p:r:m:s:d:f::o:", opt, NULL))) {
		switch (op) {
		case 'p':
			c_header.pid = strtoll(optarg, NULL, 0);
			if (c_header.pid < EXOFS_MIN_PID) {
				usage();
				return 1;
			}
			break;
		case 'r':
			c_header.raid_no = atoi(optarg);
			switch (c_header.raid_no) {
			case 0:
			case 4:
			case 5:
			case 6:
				break;
			default:
				usage();
				return 1;
			}
			break;
		case 'm':
			c_header.mirrors = atoi(optarg);
			break;
		case 's':
			c_header.stripe_unit = atoi(optarg) * EXOFS_BLKSIZE;
			break;
		case 'g':
			c_header.group_width = atoi(optarg);
			break;
		case 't':
			c_header.group_depth = atoi(optarg);
			break;

		case 'd':
			if (dev.path && dev.format_size_meg) {
				ret = _format(&dev);
				if (ret)
					return ret;
			}
			dev.path = strdup(optarg);
			if (c_header.num_ods >= max_devs) {
				max_devs += 16;
				devs = realloc(devs, sizeof(*devs) * max_devs);
				if (!devs)
					return ENOMEM;
			}
			devs[c_header.num_ods++] = dev.path;
			break;

		case 'f':
			dev.format_size_meg = optarg ?
				_LLU(strtoll(optarg, NULL, 0)) :
				EXOFS_FORMAT_ALL;
			if (!dev.format_size_meg) /* == 0 is accepted */
				dev.format_size_meg = EXOFS_FORMAT_ALL;
			break;
		case 'o':
			dev.osdname = strdup(optarg);
			break;
		default:
			usage();
			return 1;
		}
	}
	if (dev.path) {
		if (dev.format_size_meg) {
			ret = _format(&dev);
			if (ret)
				return ret;
		}
	} else {
		usage();
		return 1;
	}

	ret = _mkfs(devs, &c_header);

	free(devs);
	return ret;
}
