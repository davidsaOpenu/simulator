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
#ifndef __KinU_TIME_H__
#define __KinU_TIME_H__

#include <sys/time.h>
#include <asm/div64.h>

#endif /* ndef __KinU_TIME_H__ */
