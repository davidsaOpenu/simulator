/*
 * Symbols from the OSD and other specifications.
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
#ifndef __OSD_DEFS_H
#define __OSD_DEFS_H
/*
 * Shared defitions for OSD target and initiator.
 */
#define VARLEN_CDB 0x7f
#define OSD_CDB_SIZE 236
#define OSD_MAX_SENSE 252
#define OSD_CRYPTO_KEYID_SIZE 32
#define OSD_SYS_ID_SIZE 20

/* varlen cdb service actions for osd2r01 */
#define OSD_APPEND		        	0x8887
#define OSD_CLEAR                               0x8889
#define OSD_COPY_USER_OBJECTS                   0x8893
#define OSD_CREATE			        0x8882
#define OSD_CREATE_AND_WRITE		        0x8892
#define OSD_CREATE_CLONE                        0x88a8
#define OSD_CREATE_COLLECTION		        0x8895
#define OSD_CREATE_PARTITION		        0x888b
#define OSD_CREATE_SNAPSHOT                     0x88a9
#define OSD_CREATE_USER_TRACKING_COLLECTION     0x8894
#define OSD_DETACH_CLONE                        0x88aa
#define OSD_FLUSH		        	0x8888
#define OSD_FLUSH_COLLECTION	        	0x889a
#define OSD_FLUSH_OSD		        	0x889c
#define OSD_FLUSH_PARTITION	        	0x889b
#define OSD_FORMAT_OSD			        0x8881
#define OSD_GET_ATTRIBUTES	        	0x888e
#define OSD_GET_MEMBER_ATTRIBUTES        	0x88a2
#define OSD_LIST		        	0x8883
#define OSD_LIST_COLLECTION	        	0x8897
#define OSD_OBJECT_STRUCTURE_CHECK              0x8880
#define OSD_PERFORM_SCSI_COMMAND        	0x8f7c
#define OSD_PERFORM_TASK_MGMT_FUNC      	0x8f7d
#define OSD_PUNCH                               0x8884
#define OSD_QUERY		        	0x88a0
#define OSD_READ			        0x8885
#define OSD_READ_MAP                            0x88b1
#define OSD_READ_MAPS_AND_COMPARE               0x88b2
#define OSD_REFRESH_SNAPSHOT_OR_CLONE           0x88ab
#define OSD_REMOVE		        	0x888a
#define OSD_REMOVE_COLLECTION	        	0x8896
#define OSD_REMOVE_MEMBER_OBJECTS       	0x88a1
#define OSD_REMOVE_PARTITION	         	0x888c
#define OSD_RESTORE_PARTITION_FROM_SNAPSHOT     0x88ac
#define OSD_SET_ATTRIBUTES	        	0x888f
#define OSD_SET_KEY		        	0x8898
#define OSD_SET_MASTER_KEY	        	0x8899
#define OSD_SET_MEMBER_ATTRIBUTES       	0x88a3
#define OSD_WRITE		        	0x8886

/* custom definitions */
#define OSD_CAS				0x888d
#define OSD_FA				0x8890
#define OSD_COND_SETATTR		0x8891
#define OSD_GEN_CAS			0x88a5               

/* Data Distribution Types */
#define DDT_CONTIG	0x0
#define DDT_SGL		0x1
#define DDT_VEC		0x2
#define DDT_RES		0x3


#define SAM_STAT_GOOD            0x00
#define SAM_STAT_CHECK_CONDITION 0x02
#define SAM_STAT_CONDITION_MET   0x04
#define SAM_STAT_BUSY            0x08
#define SAM_STAT_INTERMEDIATE    0x10
#define SAM_STAT_INTERMEDIATE_CONDITION_MET 0x14
#define SAM_STAT_RESERVATION_CONFLICT 0x18
#define SAM_STAT_COMMAND_TERMINATED 0x22
#define SAM_STAT_TASK_SET_FULL   0x28
#define SAM_STAT_ACA_ACTIVE      0x30
#define SAM_STAT_TASK_ABORTED    0x40

/* OSD object constants, osd2r04 sec 4.6.1, 4.6.6 */
#define ROOT_PID 0LLU
#define ROOT_OID 0LLU
#define PARTITION_PID_LB 0x10000LLU
#define PARTITION_OID 0x0LLU
#define OBJECT_PID_LB 0x10000LLU
#define SPONTANEOUS_COLLECTION_PID_LB 0x10000LLU
#define SPONTANEOUS_COLLECTION_OID_LB 0x1082LLU
#define TRACKING_COLLECTION_PID_LB 0x10000LLU
#define TRACKING_COLLECTION_OID_LB 0x0001LLU
#define COLLECTION_PID_LB OBJECT_PID_LB
#define USEROBJECT_PID_LB OBJECT_PID_LB
#define WELL_KNOWN_COLLECTION_PID_LB OBJECT_PID_LB
#define LINKED_COLLECTION_PID_LB OBJECT_PID_LB
#define USER_TRACKING_COLLECTION_OID_LB OBJECT_PID_LB
#define OBJECT_OID_LB OBJECT_PID_LB 
#define COLLECTION_OID_LB COLLECTION_PID_LB
#define USEROBJECT_OID_LB USEROBJECT_PID_LB
#define WELL_KNOWN_COLLECTION_OID_LB 0x1000
#define USER_TRACKING_COLLECTION_OID_LB OBJECT_PID_LB
#define LINKED_COLLECTION_OID_LB OBJECT_PID_LB


/* CDB continuation descriptors, osd2r04 sec 5.4.1 */
#define NO_MORE_DESCRIPTORS 0x0000
#define SCATTER_GATHER_LIST 0x0001
#define QUERY_LIST 0x0002
#define USER_OBJECT 0x0100
#define COPY_USER_OBJECT_SOURCE 0x0101
#define EXTENSION_CAPABILITIES 0xFFEE

/* object duplication methods, osd2r04 sec 4.13.3 */
#define DEFAULT 0x00
#define SPACE_EFFICIENT 0x01
#define PRE_ALLOCATED 0x41
#define BYTE_BY_BYTE 0x81
#define FASTER_COPY 0xFD
#define HIGHER_DATA 0xFE
#define DO_NOT_CARE 0xFF

/* object types, osd2r00 section 4.9.2.2.1 table 9 */
enum {
	ROOT = 0x01,
	PARTITION = 0x02,
	COLLECTION = 0x40,
	USEROBJECT = 0x80,
	ILLEGAL_OBJ = 0x00 /* XXX: this is not in standard */
};

/* attribute page ranges, osd2r01 sec 4.7.3 */
enum {
	USEROBJECT_PG = 0x0U,
	PARTITION_PG  = 0x30000000U,
	COLLECTION_PG = 0x60000000U,
	ROOT_PG       = 0x90000000U,
	RESERVED_PG   = 0xC0000000U,
	ANY_PG        = 0xF0000000U,
	CUR_CMD_ATTR_PG = 0xFFFFFFFEU,
	GETALLATTR_PG = 0xFFFFFFFFU,
};

/* attribute page sets, further subdivide the above, osd2r01 sec 4.7.3 */
enum {
	STD_PG_LB = 0x0,
	STD_PG_UB = 0x7F,
	RSRV_PG_LB = 0x80,
	RSRV_PG_UB = 0x7FFF,
	OSTD_PG_LB = 0x8000,
	OSTD_PG_UB = 0xEFFF,
	MSPC_PG_LB = 0xF000,
	MSPC_PG_UB = 0xFFFF,
	LUN_PG_LB = 0x10000,
	LUN_PG_UB = 0x1FFFFFFF,
	VEND_PG_LB = 0x20000000,
	VEND_PG_UB = 0x2FFFFFFF
};

/* osd2r01, Section 4.7.4 */
enum {
	ATTRNUM_LB = 0x0,
	ATTRNUM_UB = 0xFFFFFFFE,
	ATTRNUM_INFO = ATTRNUM_LB,
	ATTRNUM_UNMODIFIABLE = 0xFFFFFFFF,
	ATTRNUM_GETALL = ATTRNUM_UNMODIFIABLE
};

/* osd2r00, Table 5 Section 4.7.5 */
enum {
	USEROBJECT_DIR_PG = (USEROBJECT_PG + 0x0),
	PARTITION_DIR_PG = (PARTITION_PG + 0x0),
	COLLECTION_DIR_PG = (COLLECTION_PG + 0x0),
	ROOT_DIR_PG = (ROOT_PG + 0x0)
};

/* (selected) attribute pages defined by osd spec, osd2r01 sec 7.1.2.1 */
enum {
	USER_DIR_PG = 0x0,
	USER_INFO_PG = 0x1,
	ROOT_INFO_PG = (ROOT_PG + 0x1),
	USER_QUOTA_PG = 0x2,
	USER_TMSTMP_PG = 0x3,
	USER_COLL_PG = 0x4,
	USER_POLICY_PG = 0x5,
	USER_ATOMICS_PG = 0x6,
};

/* in all attribute pages, attribute number 0 is a 40-byte identification */
#define PAGE_ID (0x0)
#define ATTR_PAGE_ID_LEN (40)

/* current command attributes page constants, osd2r01 sec 7.1.2.24 */
enum {
	/* individiual attribute items (pageid always at 0) */
	CCAP_RICV = 0x1,
	CCAP_OBJT = 0x2,
	CCAP_PID = 0x3,
	CCAP_OID = 0x4,
	CCAP_APPADDR = 0x5,

	/* lengths of the items */
	CCAP_RICV_LEN = OSD_CRYPTO_KEYID_SIZE,
	CCAP_OBJT_LEN = 1,
	CCAP_PID_LEN = 8,
	CCAP_OID_LEN = 8,
	CCAP_APPADDR_LEN = 8,

	/* offsets when retrieved in page format (page num and len at 0) */
	CCAP_RICV_OFF = 8,
	CCAP_OBJT_OFF = 28+12,
	CCAP_PID_OFF = 32+12,
	CCAP_OID_OFF = 40+12,
	CCAP_APPADDR_OFF = 48+12,
	CCAP_TOTAL_LEN = 56+12,
};

/* userobject timestamp attribute page osd2r01 sec 7.1.2.18 */
enum {
	/* attributes */
	UTSAP_CTIME = 0x1,
	UTSAP_ATTR_ATIME = 0x2,
	UTSAP_ATTR_MTIME = 0x3,
	UTSAP_DATA_ATIME = 0x4,
	UTSAP_DATA_MTIME = 0x5,

	/* length of attributes */
	UTSAP_CTIME_LEN = 6,
	UTSAP_ATTR_ATIME_LEN = 6,
	UTSAP_ATTR_MTIME_LEN = 6,
	UTSAP_DATA_ATIME_LEN = 6,
	UTSAP_DATA_MTIME_LEN = 6,

	/* offsets */
	UTSAP_CTIME_OFF = 8,
	UTSAP_ATTR_ATIME_OFF = 14,
	UTSAP_ATTR_MTIME_OFF = 20,
	UTSAP_DATA_ATIME_OFF = 26,
	UTSAP_DATA_MTIME_OFF = 32,
	UTSAP_TOTAL_LEN = 38,
};

/* userobject information attribute page osd2r01 sec 7.1.2.11 */
enum {
	/* attributes */
	UIAP_PID = 0x1,
	UIAP_OID = 0x2,
	UIAP_USERNAME = 0x9,
	UIAP_USED_CAPACITY = 0x81,
	UIAP_LOGICAL_LEN = 0x82,
	PARTITION_CAPACITY_QUOTA = 0x10001,

	/* lengths */
	UIAP_PID_LEN = 8,
	UIAP_OID_LEN = 8,
	UIAP_USED_CAPACITY_LEN = 8,
	UIAP_LOGICAL_LEN_LEN = 8,
};

/* Root information attribute page osd2r01 sec 7.1.2.8 */
enum {
	/* attributes */
	RIAP_OSD_SYSTEM_ID            = 0x3,   /* 20       */
	RIAP_VENDOR_IDENTIFICATION    = 0x4,   /* 8        */
	RIAP_PRODUCT_IDENTIFICATION   = 0x5,   /* 16       */
	RIAP_PRODUCT_MODEL            = 0x6,   /* 32       */
	RIAP_PRODUCT_REVISION_LEVEL   = 0x7,   /* 4        */
	RIAP_PRODUCT_SERIAL_NUMBER    = 0x8,   /* variable */
	RIAP_OSD_NAME                 = 0x9,   /* variable */
	RIAP_TOTAL_CAPACITY           = 0x80,  /* 8        */
	RIAP_USED_CAPACITY            = 0x81,  /* 8        */
	RIAP_NUMBER_OF_PARTITIONS     = 0xC0,  /* 8        */
	RIAP_CLOCK                    = 0x100, /* 6        */

	/* lengths */
	RIAP_OSD_SYSTEM_ID_LEN            = OSD_SYS_ID_SIZE,
	RIAP_VENDOR_IDENTIFICATION_LEN    = 8,
	RIAP_PRODUCT_IDENTIFICATION_LEN   = 16,
	RIAP_PRODUCT_MODEL_LEN            = 32,
	RIAP_PRODUCT_REVISION_LEVEL_LEN   = 4,
	RIAP_PRODUCT_SERIAL_NUMBER_LEN    = 0, //variable
	RIAP_OSD_NAME_LEN                 = 0, //variable
	RIAP_TOTAL_CAPACITY_LEN           = 8,
	RIAP_USED_CAPACITY_LEN            = 8,
	RIAP_NUMBER_OF_PARTITIONS_LEN     = 8,
	RIAP_CLOCK_LEN                    = 6,
};

/* userobject collections attribute page osd2r01 Sec 7.1.2.19 */
enum {
	UCAP_COLL_PTR_LB = 0x1,
	UCAP_COLL_PTR_UB = 0xFFFFFF00,
};

/* userobject atomics page. Only userobjects have atomics attribute page. */
enum {
	UAP_CAS = 0x1,
	UAP_FA = 0x2,
};

enum {
        GETFIELD_SETVALUE = 0x1,
	GETPAGE_SETVALUE = 0x2,
	GETLIST_SETLIST = 0x3
};

/* osd2r03 6.22 Table 103, requested map type. */
enum {
        ALL_TYPE = 0x0000,
	WRITTEN_DATA = 0x0001,
	DATA_HOLE = 0x0002,
	DAMAGED_DATA = 0x0003,
	DAMAGED_ATTRIBUTES = 0x8000
};
/*osd2r03 4.14.5 Data-In and Data-Out buffer offsets*/
#define OFFSET_UNUSED (0xFFFFFFFFU)

#endif /* __OSD_DEFS_H */
