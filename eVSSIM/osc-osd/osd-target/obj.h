/*
 * Object table.
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
#ifndef __OBJ_H
#define __OBJ_H

#include <sqlite3.h>
#include "osd-types.h"

int obj_initialize(struct db_context *dbc);

int obj_finalize(struct db_context *dbc);

const char *obj_getname(struct db_context *dbc);

int obj_insert(struct db_context *dbc, uint64_t pid, uint64_t oid, 
	       uint32_t type);

int obj_delete(struct db_context *dbc, uint64_t pid, uint64_t oid);

int obj_delete_pid(struct db_context *dbc, uint64_t pid);

int obj_get_nextoid(struct db_context *dbc, uint64_t pid, uint64_t *oid);

int obj_get_nextpid(struct db_context *dbc, uint64_t *pid);

int obj_ispresent(struct db_context *dbc, uint64_t pid, uint64_t oid, 
		  int *present);

int obj_isempty_pid(struct db_context *dbc, uint64_t pid, int *isempty);

int obj_get_type(struct db_context *dbc, uint64_t pid, uint64_t oid, 
		 uint8_t *obj_type);

int obj_get_oids_in_pid(struct db_context *dbc, uint64_t pid, 
			uint64_t initial_oid, uint64_t alloc_len, 
			uint8_t *outdata, uint64_t *used_outlen, 
			uint64_t *add_len, uint64_t *cont_id);

int obj_get_cids_in_pid(struct db_context *dbc, uint64_t pid, 
			uint64_t initial_cid, uint64_t alloc_len, 
			uint8_t *outdata, uint64_t *used_outlen, 
			uint64_t *add_len, uint64_t *cont_id);

int obj_get_all_pids(struct db_context *dbc, uint64_t initial_oid, 
		     uint64_t alloc_len, uint8_t *outdata,
		     uint64_t *used_outlen, uint64_t *add_len,
		     uint64_t *cont_id);
#endif /* __OBJ_H */
