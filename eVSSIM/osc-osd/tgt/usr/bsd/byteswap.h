#ifndef __TGTBSD_BYTESWAP_H__
#define __TGTBSD_BYTESWAP_H__

#ifdef __APPLE__
#include <libkern/OSByteOrder.h>

#define bswap_16(x)	OSSwapConstInt16(x)
#define bswap_32(x)	OSSwapConstInt32(x)
#define bswap_64(x)	OSSwapConstInt64(x)

#else
#include <sys/endian.h>

#define bswap_16(x) __bswap16(x)
#define bswap_32(x) __bswap32(x)
#define bswap_64(x) __bswap64(x)
#endif /* __APPLE__ */

#endif /* __TGTBSD_BYTESWAP_H__ */
