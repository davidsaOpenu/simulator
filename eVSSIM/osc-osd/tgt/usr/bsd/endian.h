#ifndef __TGTBSD_ENDIAN_H__
#define __TGTBSD_ENDIAN_H__

#include <machine/endian.h>

#ifdef __APPLE__
#define __BYTE_ORDER __DARWIN_BYTE_ORDER
#define __LITTLE_ENDIAN __DARWIN_LITTLE_ENDIAN
#define __BIG_ENDIAN __DARWIN_BIG_ENDIAN

#else

#define __BYTE_ORDER _BYTE_ORDER
#define __LITTLE_ENDIAN _LITTLE_ENDIAN
#define __BIG_ENDIAN _BIG_ENDIAN

#if _BYTE_ORDER == _LITTLE_ENDIAN

#define htonl(x)        __htonl(x)
#define htons(x)        __htons(x)
#define ntohl(x)        __ntohl(x)
#define ntohs(x)        __ntohs(x)

#else /* _BYTE_ORDER == _LITTLE_ENDIAN */

#define htonl(x) x
#define htons(x) x
#define ntohl(x) x
#define ntohs(x) x

#endif /* _BYTE_ORDER == _LITTLE_ENDIAN */

#endif /* __APPLE__ */

#endif /* __TGTBSD_ENDIAN_H__ */
