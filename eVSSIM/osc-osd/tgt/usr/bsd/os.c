/*
 *  OS dependent services implementation for BSD platform
 *
 * Copyright (C) 2009 Boaz Harrosh <bharrosh@panasas.com>
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
#include <unistd.h>
#include <sys/disk.h>
#include <sys/ioctl.h>

#include <linux/fs.h>

#include <sys/socket.h>
#include <stdio.h>

#include "os.h"

int os_sync_file_range(int fd, __off64_t offset, __off64_t bytes)
{
	return fsync(fd);
}

int os_ipc_perm(int fd)
{
	return 0;
}

int os_oom_adjust(void)
{
	return 0;
}

int os_blockdev_size(int fd, uint64_t *size)
{
#ifdef __APPLE__
	uint32_t size32;
	int err = ioctl(fd, DKIOCGETBLOCKSIZE, &size32);
	*size = size32;
	return err;
#else
	return ioctl(fd, DIOCGMEDIASIZE, &size);
#endif /*  else __APPLE__ */
}

int os_semtimedop (int __semid, struct sembuf *__sops, size_t __nsops,
		       __const struct timespec *__timeout)
{
	return -1;
}

int bsd_setsockopt(int s, int level, int optname, const void *optval,
		   socklen_t optlen)
{
	if (optname == BSD_NOT_SUPPORTED)
		return 0;

#undef setsockopt
	return setsockopt(s, level, optname, optval, optlen);
}

int os_nr_open(void)
{
#ifdef __APPLE__
	FILE *fp;
	char s[1024];
	int max;

	if (!(fp = popen("sysctl -a kern.maxfilesperproc", "r"))) {
		fprintf(stderr, "can't call sysctl\n");
		return 0;
	}

	if ((fgets(s, sizeof(s), fp) == NULL) ||
	    (sscanf(s, "kern.maxfilesperproc: %d", &max) != 1)) {
		fprintf(stderr, "can't read sysctl kern.maxfilesperproc\n");
		max = 0;
	}
	pclose(fp);
	return max;
#else
	/* Just Let tgtd play with it's default max_files, as it likes them */
	return 0;
#endif /*  else __APPLE__ */
}
