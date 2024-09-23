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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sqlite3.h>
#include <errno.h>
#include <sys/stat.h>
#include <assert.h>

#include "osd-types.h"
#include "osd.h"
#include "db.h"
#include "obj.h"
#include "coll.h"
#include "osd-util/osd-util.h"
#include "attr.h"

extern const char osd_schema[];

static int check_membership(void *arg, int count, char **val, 
			    char **colname)
{
	size_t i = 0;
	struct array *arr = arg;
	const char **tables = arr->a;

	for (i = 0; i < arr->ne; i++)
		if (strcmp(tables[i], val[0]) == 0)
			return 0;
	return -1;
}

/*
 * returns:
 * OSD_ERROR: in case no table is found
 * OSD_OK: when all tables exist
 */
static int db_check_tables(struct db_context *dbc)
{
	int i = 0;
	int ret = 0;
	char SQL[MAXSQLEN];
	char *err = NULL;
	const char *tables[] = {"attr", "obj", "coll"};
	struct array arr = {ARRAY_SIZE(tables), tables};

	sprintf(SQL, "SELECT name FROM sqlite_master WHERE type='table' "
		" ORDER BY name;");
	ret = sqlite3_exec(dbc->db, SQL, check_membership, &arr, &err);
	if (ret != SQLITE_OK) {
		osd_error("%s: query %s failed: %s", __func__, SQL, err);
		sqlite3_free(err);
		return OSD_ERROR;
	}

	return OSD_OK;
}


/*
 *  <0: error
 * ==0: success
 * ==1: new db opened, caller must initialize tables
 */
int osd_db_open(const char *path, struct osd_device *osd)
{
	int ret;
	struct stat sb;
	char SQL[MAXSQLEN];
	char *err = NULL;
	int is_new_db = 0;

	ret = stat(path, &sb);
	if (ret == 0) {
		if (!S_ISREG(sb.st_mode)) {
			osd_error("%s: path %s not a regular file", 
				  __func__, path);
			ret = 1;
			goto out;
		}
	} else {
		if (errno != ENOENT) {
			osd_error("%s: stat path %s", __func__, path);
			goto out;
		}

		/* sqlite3 will create it for us */
		is_new_db = 1;
	}

	osd->dbc = Calloc(1, sizeof(*osd->dbc));
	if (!osd->dbc) {
		ret = -ENOMEM;
		goto out;
	}

	ret = sqlite3_open(path, &(osd->dbc->db));
	if (ret != SQLITE_OK) {
		osd_error("%s: open db %s", __func__, path);
		ret = OSD_ERROR;
		goto out_free_dbc;
	}

	if (is_new_db) {
		/* build tables from schema file */
		ret = sqlite3_exec(osd->dbc->db, osd_schema, NULL, NULL, &err);
		if (ret != SQLITE_OK) {
			sqlite3_free(err);
			ret = OSD_ERROR;
			goto out_close_db;
		}
	} else {
		/* existing db, check for tables */
		ret = db_check_tables(osd->dbc);
		if (ret != OSD_OK)
			goto out_close_db;
	}

	/* initialize dbc fields */
	ret = db_initialize(osd->dbc);
	if (ret != OSD_OK) {
		ret = OSD_ERROR;
		goto out_close_db;
	}

	if (is_new_db) 
		ret = 1;
	goto out;

out_close_db:
	sqlite3_close(osd->dbc->db);
out_free_dbc:
	free(osd->dbc);
	osd->dbc = NULL;
out:
	return ret;
}


int osd_db_close(struct osd_device *osd)
{
	int ret = 0;

	assert(osd && osd->dbc && osd->dbc->db);

	db_finalize(osd->dbc);
	sqlite3_close(osd->dbc->db);
	free(osd->dbc);
	osd->dbc = NULL;

	return OSD_OK;
}


int db_initialize(struct db_context *dbc)
{
	int ret = 0;

	if (dbc == NULL) {
		ret = -EINVAL;
		goto out;
	}

	ret = coll_initialize(dbc);
	if (ret != OSD_OK) 
		goto finalize_coll;
	ret = obj_initialize(dbc);
	if (ret != OSD_OK)
		goto finalize_obj;
	ret = attr_initialize(dbc);
	if (ret != OSD_OK)
		goto finalize_attr;

	ret = OSD_OK;
	goto out;

finalize_attr:
	attr_finalize(dbc);
finalize_obj:
	obj_finalize(dbc);
finalize_coll:
	coll_finalize(dbc);
out:
	return ret;
}


int db_finalize(struct db_context *dbc)
{
	int ret = 0;

	if (dbc == NULL)
		return -EINVAL;

	ret = 0;
	ret |= coll_finalize(dbc);
	ret |= obj_finalize(dbc);
	ret |= attr_finalize(dbc);
	if (ret == OSD_OK)
		return OSD_OK;

	return OSD_ERROR;
}


int db_begin_txn(struct db_context *dbc)
{
	int ret = 0;
	char *err = NULL;

	assert(dbc && dbc->db);

	ret = sqlite3_exec(dbc->db, "BEGIN TRANSACTION;", NULL, NULL, &err);
	if (ret != SQLITE_OK) {
		osd_error("pragma failed: %s", err);
		sqlite3_free(err);
		return OSD_ERROR;
	}

	return OSD_OK;
}


int db_end_txn(struct db_context *dbc)
{
	int ret = 0;
	char *err = NULL;

	TICK_TRACE(db_end_txn);
	assert(dbc && dbc->db);

	ret = sqlite3_exec(dbc->db, "END TRANSACTION;", NULL, NULL, &err);
	if (ret != SQLITE_OK) {
		osd_error("pragma failed: %s", err);
		return OSD_ERROR;
	}

	TICK_TRACE(db_end_txn);
	return OSD_OK;
}


int db_exec_pragma(struct db_context *dbc)
{
	int ret = 0;
	char *err = NULL;
	char SQL[MAXSQLEN];

	assert(dbc && dbc->db);

	sprintf(SQL,
		"PRAGMA synchronous = OFF; " /* sync off */
		"PRAGMA auto_vacuum = 1; "   /* reduce db size on delete */
		"PRAGMA count_changes = 0; " /* ignore count changes */
		"PRAGMA temp_store = 0; "    /* memory as scratchpad */
	       );
	ret = sqlite3_exec(dbc->db, SQL, NULL, NULL, &err);
	if (ret != SQLITE_OK) {
		osd_error("pragma failed: %s", err);
		sqlite3_free(err);
		return OSD_ERROR;
	}

	return OSD_OK;
}


static int callback(void *ignore, int count, char **val, char **colname)
{
	printf("%s: PRAGMA %s = %s\n", __func__, colname[0], val[0]);
	return 0;
}


int db_print_pragma(struct db_context *dbc)
{
	int ret = 0;
	char *err = NULL;
	char SQL[MAXSQLEN];

	assert(dbc && dbc->db);

	sprintf(SQL,
		" PRAGMA synchronous;"
		" PRAGMA auto_vacuum;"
		" PRAGMA auto_vacuum;"
		" PRAGMA temp_store;"
	       );
	ret = sqlite3_exec(dbc->db, SQL, callback, NULL, &err);
	if (ret != SQLITE_OK) {
		osd_error("pragma failed: %s", err);
		sqlite3_free(err);
		return OSD_ERROR;
	}

	return OSD_OK;
}


/*
 * print sqlite errmsg 
 */
void __attribute__((format(printf,2,3)))
error_sql(sqlite3 *db, const char *fmt, ...)
{
    va_list ap;

    fprintf(stderr, "osd: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, ": %s.\n", sqlite3_errmsg(db));
}


/*
 * exec_dms: executes prior prepared and bound data manipulation statement.
 * Only SQL statements with 'insert' or 'delete' may call this helper
 * function.
 *
 * returns:
 * OSD_ERROR: in case of error
 * OSD_OK: on success
 * OSD_REPEAT: in case of sqlite_schema error, statements are prepared again,
 * 	hence values need to be bound again.
 */
int db_exec_dms(struct db_context *dbc, sqlite3_stmt *stmt, int ret, 
		const char *func)
{
	int bound;
	
	TICK_TRACE(db_exec_dms);
	bound = (ret == SQLITE_OK);
	if (!bound) {
		error_sql(dbc->db, "%s: bind failed", func);
		goto out_reset;
	}

	do {
	    TICK_TRACE(sqlite3_step);
	    ret = sqlite3_step(stmt);
	} while (ret == SQLITE_BUSY);

out_reset:
	ret = db_reset_stmt(dbc, stmt, bound, func);
	TICK_TRACE(db_exec_dms);
	return ret;
}


/*
 * this function executes id retrieval statement. Only the functions
 * retireiving a list of oids, cids or pids may use this function
 *
 * returns:
 * OSD_ERROR: in case of any error
 * OSD_OK: on success
 * OSD_REPEAT: in case of sqlite_schema error, statements are prepared again,
 * 	hence values need to be bound again.
 */
int db_exec_id_rtrvl_stmt(struct db_context *dbc, sqlite3_stmt *stmt, 
			  int ret, const char *func, uint64_t alloc_len, 
			  uint8_t *outdata, uint64_t *used_outlen, 
			  uint64_t *add_len, uint64_t *cont_id)
{
	uint64_t len = 0;
	int bound = (ret == SQLITE_OK);

	if (!bound) {
		error_sql(dbc->db, "%s: bind failed", func);
		goto out_reset;
	}

	len = 0;
	*add_len = 0;
	*cont_id = 0;
	*used_outlen = 0;
	while (1) {
		ret = sqlite3_step(stmt);
		if (ret == SQLITE_ROW) {
			if ((alloc_len - len) >= 8) {
				set_htonll(outdata, 
					   sqlite3_column_int64(stmt, 0));
				outdata += 8;
				len += 8;
			} else if (*cont_id == 0) {
				*cont_id = sqlite3_column_int64(stmt, 0);
			}
			/* handle overflow: osd2r01 Sec 6.14.2 */
			if (*add_len + 8 > *add_len) {
				*add_len += 8;
			} else {
				/* terminate since add_len overflew */
				*add_len = (uint64_t) -1;
				break;

			} 
		} else if (ret == SQLITE_BUSY) {
			continue;
		} else {
			break;
		}
	}

out_reset:
	ret = db_reset_stmt(dbc, stmt, bound, func);
	if (ret == OSD_OK)
		*used_outlen = len;
	return ret;
}

