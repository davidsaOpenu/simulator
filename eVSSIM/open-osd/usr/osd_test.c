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
#include "osd_ktests.h"

/* missing from old distro headers*/
#ifndef ENOIOCTLCMD
#  define ENOIOCTLCMD	515	/* No ioctl command */
#endif

static void usage(void)
{
	static char msg[] = {
	"usage: osd_test [options] /dev/osdX\n"
	"--usrlib=[0/1] -u [0/1]\n"
	"       0/1 - Run/Do NOT run user-mode library test\n"
	"       default == 1 (0 must be specified to not run this test)\n"
	"--kernel=[0/1] -k [0/1]\n"
	"       0/1  - Run/Do not run kernel test\n"
	"       default == 0\n"
	"  Note: - The output of the tests is in Kernel messages log file\n"
	"        - For this to actually run osd_ktests.ko must be insmod(ed)\n"
	"--continue\n"
	"       If specified continue to next test even if first failed\n"
	"       default == don't continue\n"
	"\n"
	"/dev/osdX is the osd char-device to run the test on. Note that in\n"
	"the case of a user-mode test, the test is run via /dev/bsg/ an\n"
	"automatic translation is done from the given /dev/osdX to the\n"
	"corresponding bsg entry, by query of:\n"
	"        /sys/class/scsi_osd/osdX/device/bsg:(.*)\n"
	"\n"
	"When both tests are given the --usrlib test runs first.\n"
	};

	printf(msg);
}

static int test_kernel(const char *path)
{
	int osd_file, ret;

	osd_file = open(path, O_RDWR);
	if (osd_file < 0) {
		fprintf(stderr, "Error opening <%s>: %s\n", path,
				strerror(errno));
		return errno;
	}

	ret = ioctl(osd_file, OSD_TEST_ALL, 0);
	if (ret) {
		if (ENOIOCTLCMD == errno)
			fprintf(stderr,
			   "No Kernel test module is loaded please run:\n"
			   "[open-osd]$ insmod ./drivers/scsi/osd/osd_ktests.ko"
			);
		else
			printf("ioctl 17 returned %d errno=%d: %s\n", ret,
				errno, strerror(errno));
		ret = errno;
	}

	close(osd_file);
	return ret;
}

static int test_usrlib(const char *path)
{
	struct osd_dev *od;
	int ret;

	ret = osd_open(path, &od);
	if (ret)
		return ret;

	ret = do_test_17(od, OSD_TEST_ALL, 0);

	osd_close(od);

	if (ret) {
		/* do_test_17() is a kernel API it returns negative errors */
		ret = -ret;
		printf("do_test_17 returned %d: %s\n", ret, strerror(ret));
	}

	return ret;
}

int main(int argc, char *argv[])
{
	struct option opt[] = {
		{.name = "kernel", .has_arg = 2, .flag = NULL, .val = 'k'} ,
		{.name = "usrlib", .has_arg = 2, .flag = NULL, .val = 'u'} ,
		{.name = "continue", .has_arg = 0, .flag = NULL, .val = 'c'} ,
		{.name = 0, .has_arg = 0, .flag = 0, .val = 0} ,
	};
	int do_kernel = 0;
	int do_usrlib = 1;
	int t_continue = 0;
	int ret = 0;
	char op;

	while ((op = getopt_long(argc, argv, "k::u::", opt, NULL)) != -1) {
		switch (op) {
		case 'k':
			do_kernel = optarg ? atoi(optarg) : 1;
			break;

		case 'u':
			do_usrlib = optarg ? atoi(optarg) : 1;
			break;
		case 'c':
			t_continue = 1;
			break;
		}
	}

	argc -= optind;
	argv += optind;

	if (argc <= 0) {
		usage();
		return 1;
	}

	if (do_usrlib) {
		ret = test_usrlib(argv[argc-1]);
		if (ret && !t_continue)
			return ret;
	}

	if (do_kernel) {
		ret = test_kernel(argv[argc-1]);
		if (ret && !t_continue)
			return ret;
	}

	return ret;
}
