/*
 * Sense-related symbols.
 *
 * Copyright (C) 2007 OSD Team <pvfs-osd@osc.edu>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __OSD_SENSE_H
#define __OSD_SENSE_H

/* scsi sense keys (SSK) defined in table 27, SPC3 r23 */
#define OSD_SSK_NO_SENSE (0x00)
#define OSD_SSK_RECOVERED_ERROR (0x01)
#define OSD_SSK_NOT_READY (0x02)
#define OSD_SSK_MEDIUM_ERROR (0x03)
#define OSD_SSK_HARDWARE_ERROR (0x04)
#define OSD_SSK_ILLEGAL_REQUEST (0x05)
#define OSD_SSK_UNIT_ATTENTION (0x06)
#define OSD_SSK_DATA_PROTECTION (0x07)
#define OSD_SSK_BLANK_CHECK (0x08)
#define OSD_SSK_VENDOR_SPECIFIC (0x09)
#define OSD_SSK_COPY_ABORTED (0x0A)
#define OSD_SSK_ABORTED_COMMAND (0x0B)
#define OSD_SSK_OBSOLETE_SENSE_KEY (0x0C)
#define OSD_SSK_VOLUME_OVERFLOW (0x0D)
#define OSD_SSK_MISCOMPARE (0x0E)
#define OSD_SSK_RESERVED_SENSE_KEY (0x0F)

/* OSD specific additional sense codes (ASC), defined in table 28 SPC3 r23.
 * OSD specific ASC from osd2 T10 r10 section 4, 5, 6, 7 */
#define OSD_ASC_INVALID_COMMAND_OPCODE (0x2000)
#define OSD_ASC_INVALID_DATA_OUT_BUF_INTEGRITY_CHK_VAL (0x260F)
#define OSD_ASC_INVALID_FIELD_IN_CDB (0x2400)
#define OSD_ASC_INVALID_FIELD_IN_PARAM_LIST (0x2600)
#define OSD_ASC_LOGICAL_UNIT_NOT_RDY_FRMT_IN_PRGRS (0x0404)
#define OSD_ASC_NONCE_NOT_UNIQUE (0x2406)
#define OSD_ASC_NONCE_TIMESTAMP_OUT_OF_RANGE (0x2407)
#define OSD_ASC_POWER_ON_OCCURRED (0x2900)
#define OSD_ASC_PARAMETER_LIST_LENGTH_ERROR (0x1A00)
#define OSD_ASC_PART_OR_COLL_CONTAINS_USER_OBJECTS (0x2C0A)
#define OSD_ASC_READ_PAST_END_OF_USER_OBJECT (0x3B17)
#define OSD_ASC_RESERVATIONS_RELEASED (0x2A04)
#define OSD_ASC_QUOTA_ERROR (0x5507)
#define OSD_ASC_SECURITY_AUDIT_VALUE_FROZEN (0x2405)
#define OSD_ASC_SECURITY_WORKING_KEY_FROZEN (0x2406)
#define OSD_ASC_SYSTEM_RESOURCE_FAILURE (0x5500)

#endif /* __OSD_SENSE_H */
