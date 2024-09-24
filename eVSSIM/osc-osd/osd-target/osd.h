/*
 * OSD commands.
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
#ifndef __OSD_H
#define __OSD_H

#include "osd-types.h"
#include "cdb.h"

/* to make sense data for some higher error */
int osd_error_unimplemented(uint16_t action, uint8_t *sense);
int osd_error_bad_cdb(uint8_t *sense);

/* db ops */
int osd_begin_txn(struct osd_device *osd);
int osd_end_txn(struct osd_device *osd);

/*
 * Commands.
 *
 * These functions return 0 if they complete successfully.  The return
 * >0 to indicate that there are valid sense data bytes that should be
 * returned as an error.  They never return <0.
 *
 * sense: buf, 252 bytes long, initialized to NULL to be filled in with
 *   sense response data, if any
 */
int osd_append(struct osd_device *osd, uint64_t pid, uint64_t oid,
	       uint64_t len, const uint8_t *data,  
	       uint32_t cdb_cont_len, uint8_t *sense, uint8_t ddt);
int osd_clear(struct osd_device *osd, uint64_t pid, uint64_t oid,
	      uint64_t len, uint64_t offset ,uint32_t cdb_cont_len, uint8_t *sense);
int osd_copy_user_objects(struct osd_device *osd, uint64_t pid,
			  uint64_t requested_oid,
			  const struct copy_user_object_source *cuos,
			  uint8_t dupl_method, uint8_t *sense);
int osd_create(struct osd_device *osd, uint64_t pid, uint64_t requested_oid,
	       uint16_t num, uint32_t cdb_cont_len, uint8_t *sense);
int osd_create_and_write(struct osd_device *osd, uint64_t pid,
			 uint64_t requested_oid, uint64_t len, uint64_t offset,
			 const uint8_t *data, uint32_t cdb_cont_len,
			 const struct sg_list *sglist, uint8_t *sense,
			 uint8_t ddt);
int osd_create_collection(struct osd_device *osd, uint64_t pid,
			  uint64_t requested_cid, uint32_t cdb_cont_len, uint8_t *sense);
int osd_create_partition(struct osd_device *osd, uint64_t requested_pid, uint32_t cdb_cont_len,
                         uint8_t *sense);
int osd_create_user_tracking_collection(struct osd_device *osd, uint64_t pid, 
					uint64_t requested_cid,	uint64_t source_cid,
					uint32_t cdb_cont_len, uint8_t *sense);
int osd_flush(struct osd_device *osd, uint64_t pid, uint64_t oid, uint64_t len,  
	      uint64_t offset, int flush_scope, uint32_t cdb_cont_len, uint8_t *sense);
int osd_flush_collection(struct osd_device *osd, uint64_t pid, uint64_t cid,
                         int flush_scope, uint32_t cdb_cont_len, uint8_t *sense);
int osd_flush_osd(struct osd_device *osd, int flush_scope, uint32_t cdb_cont_len, uint8_t *sense);
int osd_flush_partition(struct osd_device *osd, uint64_t pid, int flush_scope, uint32_t cdb_cont_len,
                        uint8_t *sense);
int osd_format_osd(struct osd_device *osd, uint64_t capacity, uint32_t cdb_cont_len, uint8_t *sense);
int osd_getattr_page(struct osd_device *osd, uint64_t pid, uint64_t oid,
		     uint32_t page, void *outbuf, uint64_t outlen,
		     uint8_t isembedded, uint32_t *used_outlen,
		     uint32_t cdb_cont_len, uint8_t *sense);
int osd_getattr_list(struct osd_device *osd, uint64_t pid, uint64_t oid,
		     uint32_t page, uint32_t number, uint8_t *outbuf,
		     uint32_t outlen, uint8_t isembedded, uint8_t listfmt,
		     uint32_t *used_outlen, uint32_t cdb_cont_len, uint8_t *sense);
int osd_get_member_attributes(struct osd_device *osd, uint64_t pid,
			      uint64_t cid, uint32_t cdb_cont_len, uint8_t *sense);
int osd_list(struct osd_device *osd, uint8_t list_attr, uint64_t pid,
	     uint64_t alloc_len, uint64_t initial_oid,
	     struct getattr_list *get_attr, uint32_t list_id,
	     uint8_t *outdata, uint64_t *used_outlen, uint8_t *sense);
int osd_list_collection(struct osd_device *osd, uint8_t list_attr,
			uint64_t pid, uint64_t cid, uint64_t alloc_len,
			uint64_t initial_oid, struct getattr_list *get_attr,
			uint32_t list_id, uint8_t *outdata,
			uint64_t *used_outlen, uint8_t *sense);
int osd_punch(struct osd_device *osd, uint64_t pid, uint64_t oid, uint64_t len,
	      uint64_t offset, uint32_t cdb_cont_len, uint8_t *sense);
int osd_query(struct osd_device *osd, uint64_t pid, uint64_t cid,
	      uint32_t query_list_len, uint64_t alloc_len, const void *indata,
	      void *outdata, uint64_t *used_outlen, uint32_t cdb_cont_len, uint8_t *sense);
int osd_read(struct osd_device *osd, uint64_t pid, uint64_t uid, uint64_t len,
	     uint64_t offset, const uint8_t *indata, uint8_t *outdata, uint64_t *outlen,
	     const struct sg_list *sglist, uint8_t *sense, uint8_t ddt);
int osd_read_map(struct osd_device *osd, uint64_t pid, uint64_t oid, uint64_t alloc_len,
		 uint64_t offset, uint16_t map_type, uint8_t *outdata, uint64_t *used_outlen, 
		 uint32_t cdb_cont_len, uint8_t *sense);
int osd_remove(struct osd_device *osd, uint64_t pid, uint64_t oid,
               uint32_t cdb_cont_len, uint8_t *sense);
int osd_remove_collection(struct osd_device *osd, uint64_t pid, uint64_t cid,
			  uint8_t fcr, uint32_t cdb_cont_len, uint8_t *sense);
int osd_remove_member_objects(struct osd_device *osd, uint64_t pid,
			      uint64_t cid, uint32_t cdb_cont_len, uint8_t *sense);
int osd_remove_partition(struct osd_device *osd, uint64_t pid, uint32_t cdb_cont_len, uint8_t *sense);
int osd_set_attributes(struct osd_device *osd, uint64_t pid, uint64_t oid,
                       uint32_t page, uint32_t number, const void *val,
		       uint16_t len, uint8_t cmd_type, uint32_t cdb_cont_len, uint8_t *sense);
int osd_set_key(struct osd_device *osd, int key_to_set, uint64_t pid,
		uint64_t key, uint8_t seed[OSD_CRYPTO_KEYID_SIZE],
		uint8_t *sense);
int osd_set_master_key(struct osd_device *osd, int dh_step, uint64_t key,
                       uint32_t param_len, uint32_t alloc_len,
		       uint8_t *outdata, uint64_t *outlen, uint32_t cdb_cont_len, uint8_t *sense);
int osd_set_member_attributes(struct osd_device *osd, uint64_t pid,
			      uint64_t cid, struct setattr_list *set_attr,
			      uint32_t cdb_cont_len, uint8_t *sense);

int osd_write(struct osd_device *osd, uint64_t pid, uint64_t oid, 
	      uint64_t len, uint64_t offset, const uint8_t *data, 
	      const struct sg_list *sglist, uint8_t *sense, uint8_t ddt);

int osd_cas(struct osd_device *osd, uint64_t pid, uint64_t oid, uint64_t cmp,
	    uint64_t swap, uint8_t *doutbuf, uint64_t *used_outlen,
	    uint8_t *sense);

int osd_fa(struct osd_device *osd, uint64_t pid, uint64_t oid, int64_t add,
	   uint8_t *doutbuf, uint64_t *used_outlen, uint8_t *sense);

int osd_gen_cas(struct osd_device *osd, uint64_t pid, uint64_t oid,
		uint32_t page, uint32_t number, const uint8_t *cmp,
		uint16_t cmp_len, const uint8_t *swap, uint16_t swap_len,
		uint8_t **orig_val, uint16_t *orig_len, uint8_t *sense);

/* helper functions */
static inline uint64_t osd_get_created_oid(struct osd_device *osd,
					   uint32_t numoid)
{
	uint64_t oid =  osd->ccap.oid;
	if (numoid > 0)
		oid -= (numoid - 1);
	return oid;
}

#endif /* __OSD_H */
