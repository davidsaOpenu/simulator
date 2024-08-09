/*
 * User-mode emulation of <asm/unaligned.h>
 *
 * Author: Boaz Harrosh <bharrosh@panasas.com>, (C) 2009
 *
 * Copyright: See COPYING file that comes with this distribution
 *
 */
#ifndef __KinU_UNALIGNED_H__
#define __KinU_UNALIGNED_H__

#include <asm/byteorder.h>
#include <asm/posix_types.h>
#include <linux/kernel.h>
#include <linux/types.h>

#include "le_byteshift.h"
#include "be_byteshift.h"

#define cpu_to_be16(x)	__cpu_to_be16(x)
#define cpu_to_be32(x)	__cpu_to_be32(x)
#define cpu_to_be64(x)	__cpu_to_be64(x)
#define be16_to_cpu(x)	__be16_to_cpu(x)
#define be32_to_cpu(x)	__be32_to_cpu(x)
#define be64_to_cpu(x)	__be64_to_cpu(x)

#define cpu_to_le16(x)	__cpu_to_le16(x)
#define cpu_to_le32(x)	__cpu_to_le32(x)
#define cpu_to_le64(x)	__cpu_to_le64(x)
#define le16_to_cpu(x)	__le16_to_cpu(x)
#define le32_to_cpu(x)	__le32_to_cpu(x)
#define le64_to_cpu(x)	__le64_to_cpu(x)

#endif /* __KinU_UNALIGNED_H__ */
