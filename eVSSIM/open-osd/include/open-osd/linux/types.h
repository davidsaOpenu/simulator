/*
 * User-mode safe <linux/types.h>
 *
 * Description: </usr/include/linux/types.h> can not be mixed with most of the
 *              standard c headers. This file defines all Linux types used by
 *              osd code in a manner that is std headers safe.
 *              Also add assorted defintions needed by code that belong to other
 *              unavailble kernel headers
 *
 * Author: Boaz Harrosh <bharrosh@panasas.com>, (C) 2009
 *
 * Copyright: See COPYING file that comes with this distribution
 *
 */
#ifndef __KinU_TYPES_H__
#define __KinU_TYPES_H__

#include <sys/types.h>
#include <stdint.h>
#include <limits.h>
#include <assert.h>

#define __bitwise

typedef uint64_t u64;
typedef uint64_t __u64;
typedef uint32_t u32;
typedef uint32_t __u32;
typedef uint16_t u16;
typedef uint16_t __u16;
typedef uint8_t  u8;
typedef uint8_t  __u8;
typedef int32_t __s32;

typedef u64 __bitwise __le64;
typedef u32 __bitwise __le32;
typedef u16 __bitwise __le16;
typedef u8 __bitwise __le8;

typedef u64 __bitwise __be64;
typedef u32 __bitwise __be32;
typedef u16 __bitwise __be16;
typedef u8 __bitwise __be8;

#define likely(x)	__builtin_expect(!!(x), 1)
#define unlikely(x)	__builtin_expect(!!(x), 0)

#define __deprecated			__attribute__((deprecated))
#define __packed			__attribute__((packed))
#define __weak				__attribute__((weak))
#ifndef __unused
#    define __unused			__attribute__((unused))
#endif

#define BIT(nr)			(1UL << (nr))
#define ALIGN(x,a)		__ALIGN_MASK(x,(typeof(x))(a)-1)
#define __ALIGN_MASK(x,mask)	(((x)+(mask))&~(mask))

#ifndef ARRAY_SIZE
#    define ARRAY_SIZE(a) (sizeof (a) / sizeof ((a)[0]))
#endif

typedef int bool;

enum {
	false	= 0,
	true	= 1
};

#define offsetof(TYPE,MEMBER) __builtin_offsetof(TYPE,MEMBER)
#define min(x, y) ({				\
	typeof(x) _min1 = (x);			\
	typeof(y) _min2 = (y);			\
	(void) (&_min1 == &_min2);		\
	_min1 < _min2 ? _min1 : _min2; })

#define max(x, y) ({				\
	typeof(x) _max1 = (x);			\
	typeof(y) _max2 = (y);			\
	(void) (&_max1 == &_max2);		\
	_max1 > _max2 ? _max1 : _max2; })

#define MODULE_AUTHOR(s)
#define MODULE_DESCRIPTION(s)
#define MODULE_LICENSE(s)
#define EXPORT_SYMBOL(f)

#define WARN_ON(condition) ({						\
	int __ret_warn_on = !!(condition);				\
	unlikely(__ret_warn_on);					\
})
#define BUG_ON(condition) ({						\
	int __ret_warn_on = !!(condition);				\
	if (unlikely(__ret_warn_on))					\
		assert(0);						\
})

#ifndef ULLONG_MAX
#  define ULLONG_MAX (~0ULL)
#endif

#ifndef BITS_PER_LONG
#  define BITS_PER_LONG (ULONG_MAX == 0xFFFFFFFFUL ? 32 : 64)
#endif

#endif /* ndef __KinU_TYPES_H__ */
