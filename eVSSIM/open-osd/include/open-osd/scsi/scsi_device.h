/*
 * User-mode emulation of <scsi/scsi_device.h>
 *
 * Author: Boaz Harrosh <bharrosh@panasas.com>, (C) 2009
 *
 * Copyright: See COPYING file that comes with this distribution
 *
 */
#ifndef __KinU_SCSI_DEVICE_H__
#define __KinU_SCSI_DEVICE_H__

struct request_queue;

struct scsi_device {
	struct request_queue *request_queue;
};

#endif /*__KinU_SCSI_DEVICE_H__*/
