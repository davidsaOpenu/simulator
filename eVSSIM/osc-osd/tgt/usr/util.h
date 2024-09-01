#ifndef __UTIL_H__
#define __UTIL_H__

#include <byteswap.h>
#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <linux/types.h>

#include "be_byteshift.h"
#include "os.h"

#define roundup(x, y) ((((x) + ((y) - 1)) / (y)) * (y))
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define ALIGN(x,a) (((x)+(a)-1)&~((a)-1))

#if __BYTE_ORDER == __LITTLE_ENDIAN

#define __cpu_to_be16(x) bswap_16(x)
#define __cpu_to_be32(x) bswap_32(x)
#define __cpu_to_be64(x) bswap_64(x)
#define __be16_to_cpu(x) bswap_16(x)
#define __be32_to_cpu(x) bswap_32(x)
#define __be64_to_cpu(x) bswap_64(x)

#define __cpu_to_le32(x) (x)
#define __le32_to_cpu(x) (x)

#else /* __BYTE_ORDER == __LITTLE_ENDIAN */

#define __cpu_to_be16(x) (x)
#define __cpu_to_be32(x) (x)
#define __cpu_to_be64(x) (x)
#define __be16_to_cpu(x) (x)
#define __be32_to_cpu(x) (x)
#define __be64_to_cpu(x) (x)

#define __cpu_to_le32(x) bswap_32(x)
#define __le32_to_cpu(x) bswap_32(x)

#endif /* __BYTE_ORDER == __LITTLE_ENDIAN */

#define	DEFDMODE	(S_IRUSR|S_IWUSR|S_IXUSR|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH)
#define	DEFFMODE	(S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH)

#define min(x,y) ({ \
	typeof(x) _x = (x);	\
	typeof(y) _y = (y);	\
	(void) (&_x == &_y);		\
	_x < _y ? _x : _y; })

#define max(x,y) ({ \
	typeof(x) _x = (x);	\
	typeof(y) _y = (y);	\
	(void) (&_x == &_y);		\
	_x > _y ? _x : _y; })

#define min_t(type,x,y) \
	({ type __x = (x); type __y = (y); __x < __y ? __x: __y; })
#define max_t(type,x,y) \
	({ type __x = (x); type __y = (y); __x > __y ? __x: __y; })

extern int chrdev_open(char *modname, char *devpath, uint8_t minor, int *fd);
extern int backed_file_open(char *path, int oflag, uint64_t *size);
extern int set_non_blocking(int fd);

#define zalloc(size)			\
({					\
	void *ptr = malloc(size);	\
	if (ptr)			\
		memset(ptr, 0, size);	\
	else				\
		eprintf("%m\n");	\
	ptr;				\
})

static inline int before(uint32_t seq1, uint32_t seq2)
{
        return (int32_t)(seq1 - seq2) < 0;
}

static inline int after(uint32_t seq1, uint32_t seq2)
{
	return (int32_t)(seq2 - seq1) < 0;
}

/* is s2<=s1<=s3 ? */
static inline int between(uint32_t seq1, uint32_t seq2, uint32_t seq3)
{
	return seq3 - seq2 >= seq1 - seq2;
}

#define shprintf(total, buf, rest, fmt, args...)			\
do {									\
	int len;							\
	len = snprintf(buf, rest, fmt, ##args);				\
	if (len > rest)							\
		goto overflow;						\
	buf += len;							\
	total += len;							\
	rest -= len;							\
} while (0)

extern unsigned long pagesize, pageshift;

#if defined(__NR_signalfd) && defined(USE_SIGNALFD)

/*
 * workaround for broken linux/signalfd.h including
 * usr/include/linux/fcntl.h
 */
#define _LINUX_FCNTL_H

#include <linux/signalfd.h>

static inline int __signalfd(int fd, const sigset_t *mask, int flags)
{
	int fd2, ret;

	fd2 = syscall(__NR_signalfd, fd, mask, _NSIG / 8);
	if (fd2 < 0)
		return fd2;

	ret = fcntl(fd2, F_GETFL);
	if (ret < 0) {
		close(fd2);
		return -1;
	}

	ret = fcntl(fd2, F_SETFL, ret | O_NONBLOCK);
	if (ret < 0) {
		close(fd2);
		return -1;
	}

	return fd2;
}
#else
#define __signalfd(fd, mask, flags) (-1)
struct signalfd_siginfo {
};
#endif

#endif
