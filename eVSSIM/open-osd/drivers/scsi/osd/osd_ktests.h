/*
 * osd_ktests.h - Define the ktests.c API
 *
 * Copyright (C) 2008 Panasas Inc.  All rights reserved.
 *
 * Authors:
 *   Boaz Harrosh <bharrosh@panasas.com>
 *   Benny Halevy <bhalevy@panasas.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 *
 */
#ifndef __OSD_KTESTS_H__
#define __OSD_KTESTS_H__

/* Tests from osd_ktests.c */
/* TODO: Only one simple test for now. Later I will add a test definition
 *	structure that will define what tests to preform and with some
 *	parametrization, so concurrent tests could be run on same OSD lun
 *	without stepping on each other. (E.g. Format called when other tests
 *	are in progress)
 */

enum { OSD_TEST_ALL = 17 };

int do_test_17(struct osd_dev *od, unsigned cmd, unsigned long arg);

#endif /*ndef __OSD_KTESTS_H__*/
