/*
 * osd_debug.h - Some printf macros
 *
 * Copyright (C) 2008 Panasas Inc.  All rights reserved.
 *
 * Authors:
 *   Boaz Harrosh <bharrosh@panasas.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 *
 */
#ifndef __UOSD_DEBUG_H__
#define __UOSD_DEBUG_H__

#include <stdio.h>

#define OSD_ERR(fmt, a...) fprintf(stderr, "libosd: " fmt, ##a)
#define OSD_INFO(fmt, a...) printf("libosd: " fmt, ##a)

#ifdef CONFIG_SCSI_OSD_DEBUG
#define OSD_DEBUG(fmt, a...) \
	printf("libosd @%s:%d: " fmt, __func__, __LINE__, ##a)
#else
#define OSD_DEBUG(fmt, a...) do {} while (0)
#endif

/* u64 has problems with printk this will cast it to unsigned long long */
#define _LLU(x) (unsigned long long)(x)

#endif /* ndef __UOSD_DEBUG_H__ */
