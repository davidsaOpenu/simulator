#ifndef __TGTBSD_NETINET_IN_H__
#define __TGTBSD_NETINET_IN_H__

#ifdef __APPLE__
#define s6_addr8 __u6_addr.__u6_addr8
#define s6_addr16 __u6_addr.__u6_addr16
#define s6_addr32 __u6_addr.__u6_addr32
#else
/*FIXME: define _KERNEL is needed in BSD for some of the ipv6 stuff in
 *       usr/iscsi/target.c
 */
#define _KERNEL
#endif /* else __APPLE__ */

#include <../include/netinet/in.h>

#endif /* __TGTBSD_NETINET_IN_H__ */
