/*
 * Standard types.
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
#ifndef __OSD_TYPES_H
#define __OSD_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <sqlite3.h>

#include "osd-util/osd-defs.h"

struct getattr_list_entry {
	uint32_t page;
	uint32_t number;
} __attribute__((packed));

#define LIST_HDR_LEN (8) /* XXX: osd-errata */
#define ATTR_LEN_UB (0xFFFEU) /* undefined attr have length 0xFFFF, hence 
				 max length is 0xFFFE */
#define NULL_ATTR_LEN (0xFFFFU) /* osd2r00 Sec 7.1.1 */
#define NULL_PAGE_LEN (0x00) /* osd2r00 Sec 7.1.2.25 */

struct list_entry {
	uint32_t page;
	uint32_t number;
	uint8_t reserved[6];
	uint16_t len;
	union {
		void *val;
		const void *cval;
	};
} __attribute__((packed));

enum {
	LE_PAGE_OFF = offsetof(struct list_entry, page),
	LE_NUMBER_OFF = offsetof(struct list_entry, number),
	LE_LEN_OFF = offsetof(struct list_entry, len),
	LE_VAL_OFF = offsetof(struct list_entry, val),
	LE_MIN_ITEM_LEN = LE_VAL_OFF,
};

struct multiobj_list_entry {
	uint64_t oid;
	uint32_t page;
	uint32_t number;
	uint16_t len;
	void *val;
} __attribute__((packed));

enum {
	MLE_OID_OFF = offsetof(struct multiobj_list_entry, oid),
	MLE_PAGE_OFF = offsetof(struct multiobj_list_entry, page),
	MLE_NUMBER_OFF = offsetof(struct multiobj_list_entry, number),
	MLE_LEN_OFF = offsetof(struct multiobj_list_entry, len),
	MLE_VAL_OFF = offsetof(struct multiobj_list_entry, val),
	MLE_MIN_ITEM_LEN = (LE_VAL_OFF + 0x7) & ~0x7
};

/* osd2r00 Section 7.1.3.1 tab 127 */
enum {
	RTRV_ATTR_LIST = 0x01,
	RTRVD_SET_ATTR_LIST = 0x09,
	RTRVD_CREATE_MULTIOBJ_LIST = 0x0F
};

/*
 * Things that go as attributes on the root page.
 * XXX: on second thought, don't stick these in the db, just return
 * them as needed programatically.  There's plenty of other variable
 * ones that can't live in the db (clock, #objects, capacity used).
 */
struct init_attr {
	uint32_t page;
	uint32_t number;
	const char *s;
};

struct getattr_list {
	uint32_t sz;
	struct getattr_list_entry *le;
};

struct setattr_list {
	uint32_t sz;
	struct list_entry *le;
};

#define MAXSQLEN (2048UL)
#define MAXNAMELEN (256UL)
#define MAXROOTLEN (200UL)
#define BLOCK_SZ (512UL)
#define TIME_SZ (6) 

struct __attribute__((packed)) cur_cmd_attr_pg {
	uint16_t cdb_srvc_act; 	/* current cmd  */
	uint8_t ricv[OSD_CRYPTO_KEYID_SIZE]; 	/* response integrity check value */
	uint8_t obj_type;
	uint8_t reserved[3];
	uint64_t pid;
	uint64_t oid;
	uint64_t append_off;
};

struct query_criteria {
	uint8_t query_type;	/* type of query */
	uint32_t qc_cnt_limit;	/* current limit on number of queries */
	uint32_t qc_cnt;	/* current count of queries */
	uint16_t *qce_len;
	uint32_t *page;
	uint32_t *number;
	uint16_t *min_len;
	const void **min_val;
	uint16_t *max_len;
	const void **max_val;
};

/* osd2r01 Section 6.18.2 tab 90 and 92 */
#define MINQLISTLEN (8U) /* query list header(4B) + first QCE header(4B) */
#define MINQCELEN (0)    /* QCE with just header */

/* osd2r01 Section 6.18.3 tab 93 */
enum {
	ML_ODF_OFF = 12U,
	ML_ODL_OFF = 13U,
	MIN_ML_LEN = 13U /* add len + reserved + ODF */
};

struct id_cache {
	uint64_t cur_pid;  /* last pid referenced */
	uint64_t next_id;  /* next free oid/cid within partition (cur_pid) */
};

struct buffer {
	size_t sz;
	void *buf;
};

struct array {
	size_t ne;
	void *a;
};

struct id_list {
	uint64_t cnt;
	uint64_t limit;
	uint64_t *ids;
};

/* abstract declarations of db tables */
struct coll_tab;
struct obj_tab;
struct attr_tab;

/* 
 * Encapsulate all db structs in db context. each db context is handled by an
 * independent thread.
 */
struct db_context {
	sqlite3 *db;
	struct coll_tab *coll;
	struct obj_tab *obj;
	struct attr_tab *attr;
};

/*
 * 'osd_context' will replace 'osd_device' in future. Each osd context is a
 * thread safe structure allocated as a private data for the thread. Thread
 * specific state information will be contained in this struct
 */
struct osd_context {
	struct db_context *dbc;
	struct cur_cmd_attr_pg ccap;
	struct id_cache ic;
	struct id_list idl;
};


struct osd_device {
	char *root;
	struct db_context *dbc;
	struct cur_cmd_attr_pg ccap;
	struct id_cache ic;
	struct id_list idl;
};

enum {
	GATHER_VAL = 1,
	GATHER_ATTR = 2,
	GATHER_DIR_PAGE = 3,
};

enum {
	OSD_ERROR = -1,
	OSD_OK = 0,
	OSD_REPEAT = 1
};

/*
 * For TICK_TRACE annotations, default off, unless Makefile includes
 * an external header that defines this first.
 */
#ifndef TICK_TRACE
#define TICK_TRACE(x)
#endif

/* CDB continuation descriptors */

struct sg_list_entry {
	uint64_t	offset;
	uint64_t	bytes_to_transfer;
};

struct sg_list {
	uint64_t			num_entries;
	const struct sg_list_entry	*entries;
};

struct copy_user_object_source {
	uint64_t			source_pid;
	uint64_t			source_oid;
	uint8_t				reserved1:7;
	uint8_t				cpy_attr:1;
	uint8_t				freeze:1;
	uint8_t				reserved2:3;
	uint8_t				time_of_duplication:4;
	uint16_t			reserved3;
};

struct cdb_continuation_descriptor_header {
	uint16_t	type;
	uint8_t		reserved;
	uint8_t		pad_length;
	uint32_t	length;
};

#endif /* __OSD_TYPES_H */
