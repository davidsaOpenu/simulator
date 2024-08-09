#ifndef __TGT_OS_H__
#define __TGT_OS_H__

#include <inttypes.h>
#include <sys/types.h>
#include <linux/fs.h>

int os_sync_file_range(int fd, __off64_t offset, __off64_t bytes);

int os_ipc_perm(int fd);

int os_oom_adjust(void);

int os_blockdev_size(int fd, uint64_t *size);

int os_nr_open(void);

struct sembuf;
struct timespec;
int os_semtimedop (int __semid, struct sembuf *__sops, size_t __nsops,
		       __const struct timespec *__timeout);

#endif /* ndef __TGT_OS_H__*/
