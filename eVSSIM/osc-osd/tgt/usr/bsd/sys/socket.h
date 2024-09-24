#ifndef __TGTBSD_SYS_SOCKET_H__
#define __TGTBSD_SYS_SOCKET_H__

/* Overide socket.h on BSD to define Linux specific options, in which case
 * just return success.
 */

#include <../include/sys/socket.h>

#define setsockopt bsd_setsockopt

#define BSD_NOT_SUPPORTED -1
#define TCP_KEEPIDLE	BSD_NOT_SUPPORTED
#define TCP_KEEPCNT	BSD_NOT_SUPPORTED
#define TCP_KEEPINTVL	BSD_NOT_SUPPORTED
#define TCP_CORK	BSD_NOT_SUPPORTED
#define SOL_TCP		BSD_NOT_SUPPORTED

/* is in bsd/os.c */
int bsd_setsockopt(int s, int level, int optname, const void *optval,
		   socklen_t optlen);

#endif /* __TGTBSD_SYS_SOCKET_H__ */
