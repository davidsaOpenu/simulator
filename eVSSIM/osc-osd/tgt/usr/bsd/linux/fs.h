#ifndef __TGTBSD__LINUX_FS_H__
#define __TGTBSD__LINUX_FS_H__

#include <sys/stat.h>
#include <limits.h>

#define st_atim st_atimespec
#define st_mtim st_mtimespec
#define st_ctim st_ctimespec
#define st_birthtim st_birthtimespec

typedef off_t __off64_t;
#define off64_t __off64_t
#define loff_t __off64_t

#define O_LARGEFILE 0

#ifndef SYNC_FILE_RANGE_WAIT_BEFORE
#define SYNC_FILE_RANGE_WAIT_BEFORE	1
#define SYNC_FILE_RANGE_WRITE		2
#define SYNC_FILE_RANGE_WAIT_AFTER	4
#endif

static inline int sync_file_range(int fd, __off64_t offset, __off64_t nbytes,
				  int flags)
{
	return fsync(fd);
}

static inline int fdatasync(int fd)
{
	return fsync(fd);
}

#define pwrite64 pwrite
#define pread64 pread

#define stat64 stat
#define fstat64 fstat

#define lseek64 lseek

#endif /* ndef __TGTBSD__LINUX_FS_H__ */
