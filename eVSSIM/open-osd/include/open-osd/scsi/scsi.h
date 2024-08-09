/*
 * scsi/scsi.h - Some missing declarations from gcc's scsi.h file
 *
 * Author: Boaz Harrosh <bharrosh@panasas.com>, (C) 2009
 *
 * Copyright: See COPYING file that comes with this distribution
 *
 */
#ifndef __KinU_SCSI_H__
#define __KinU_SCSI_H__

/* include gcc's original from /usr/include/scsi/scsi.h first */
#include <../include/scsi/scsi.h>

/* defined in T10 SCSI Primary Commands-2 (SPC2) */
struct scsi_varlen_cdb_hdr {
	u8 opcode;        /* opcode always == VARIABLE_LENGTH_CMD */
	u8 control;
	u8 misc[5];
	u8 additional_cdb_length;         /* total cdb length - 8 */
	__be16 service_action;
	/* service specific data follows */
};

#ifndef VARIABLE_LENGTH_CMD
#  define VARIABLE_LENGTH_CMD   0x7f
#endif

#endif /*__KinU_SCSI_H__*/
