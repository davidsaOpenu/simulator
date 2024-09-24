/*
 * Database fundamentals.
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
#ifndef __DB_H
#define __DB_H

#include <sqlite3.h>
#include "osd-types.h"

int osd_db_open(const char *path, struct osd_device *osd);

int osd_db_close(struct osd_device *osd);

int db_initialize(struct db_context *dbc);

int db_finalize(struct db_context *dbc);

int db_begin_txn(struct db_context *dbc);

int db_end_txn(struct db_context *dbc);

int db_exec_pragma(struct db_context *dbc);

int db_print_pragma(struct db_context *dbc);

void error_sql(sqlite3 *db, const char *fmt, ...)
	__attribute__((format(printf,2,3))); 

/*
 * finalize the stmt, and in case of error, report it
 */
static inline void db_sqfinalize(sqlite3 *db, sqlite3_stmt *stmt,
			      const char *SQL)
{
	int ret = 0;

	if (SQL[0] != '\0')
		error_sql(db, "prepare of %s failed", SQL);
	
	ret = sqlite3_finalize(stmt);
	if (ret != SQLITE_OK)
		error_sql(db, "%s: finalize failed", __func__);
}

/*
 * reset the sql stmt and test the return status
 *
 * returns:
 * OSD_ERROR: bind failed or stmt execution failed
 * OSD_OK: success
 * OSD_REPEAT: stmt needs to be run again
 */
static inline int db_reset_stmt(struct db_context *dbc, sqlite3_stmt *stmt,
				int bound, const char *func)
{
	int ret = sqlite3_reset(stmt);
	if (!bound) {
		return OSD_ERROR;
	} else if (ret == SQLITE_OK) {
		return OSD_OK;
	} else if (ret == SQLITE_SCHEMA) {
		db_finalize(dbc);
		ret = db_initialize(dbc);
		if (ret == OSD_OK)
			return OSD_REPEAT;
	} 
	error_sql(dbc->db, "%s: stmt failed", func);
	return OSD_ERROR;
}


int db_exec_dms(struct db_context *dbc, sqlite3_stmt *stmt, int ret, 
		const char *func);

int db_exec_id_rtrvl_stmt(struct db_context *dbc, sqlite3_stmt *stmt, 
			  int ret, const char *func, uint64_t alloc_len, 
			  uint8_t *outdata, uint64_t *used_outlen, 
			  uint64_t *add_len, uint64_t *cont_id);

#endif /* __DB_H */
