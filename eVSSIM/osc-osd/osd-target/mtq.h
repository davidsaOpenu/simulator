/*
 * Multi-table queries.
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
#ifndef __MTQ_H
#define __MTQ_H

#include <sqlite3.h>
#include "osd-types.h"

int mtq_run_query(struct db_context *dbc, uint64_t pid, uint64_t cid, 
		  struct query_criteria *qc, void *outdata, 
		  uint32_t alloc_len, uint64_t *used_outlen);

int mtq_list_oids_attr(struct db_context *dbc, uint64_t pid,
		       uint64_t initial_oid, struct getattr_list *get_attr,
		       uint64_t alloc_len, void *outdata, 
		       uint64_t *used_outlen, uint64_t *add_len, 
		       uint64_t *cont_id);

int mtq_set_member_attrs(struct db_context *dbc, uint64_t pid, uint64_t cid, 
			 struct setattr_list *set_attr);

#endif /* __MTQ_H */
