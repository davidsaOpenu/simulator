/*
 * Collections.
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
#include <sqlite3.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <sys/stat.h>

#include "osd-types.h"
#include "db.h"
#include "coll.h"
#include "osd-util/osd-util.h"
#include "list-entry.h"

/*
 * coll table stores many-to-many relationship between userobjects and
 * collections. Using this table members of a collection and collections to
 * which an object belongs can be computed efficiently.
 */

static const char *coll_tab_name = "coll";
struct coll_tab {
	char *name;             /* name of the table */
	sqlite3_stmt *insert;   /* insert a row */
	sqlite3_stmt *delete;   /* delete a row */
	sqlite3_stmt *delcid;   /* delete collection cid */
	sqlite3_stmt *deloid;   /* delete oid from all collections */
	sqlite3_stmt *emptycid; /* is collection empty? */
	sqlite3_stmt *getcid;   /* get collection */
	sqlite3_stmt *getoids;  /* get objects in a collection */
	sqlite3_stmt *copyoids; /* copy oids from one collection to another */
};


/*
 * returns:
 * -ENOMEM: out of memory
 * -EINVAL: invalid args
 * -EIO: if any prepare statement fails
 *  OSD_OK: success
 */
int coll_initialize(struct db_context *dbc)
{
	int ret = 0;
	int sqlret = 0;
	char SQL[MAXSQLEN];

	if (dbc == NULL || dbc->db == NULL) {
		ret = -EINVAL;
		goto out;
	}

	if (dbc->coll != NULL) {
		if (strcmp(dbc->coll->name, coll_tab_name) != 0) {
			ret = -EINVAL;
			goto out;
		} else {
			coll_finalize(dbc);
		}
	}

	dbc->coll = Calloc(1, sizeof(*dbc->coll));
	if (!dbc->coll) {
		ret = -ENOMEM;
		goto out;
	}
	
	dbc->coll->name = strdup(coll_tab_name); 
	if (!dbc->coll->name) {
		ret = -ENOMEM;
		goto out;
	}

	sprintf(SQL, "INSERT INTO %s VALUES (?, ?, ?, ?);", dbc->coll->name);
	ret = sqlite3_prepare(dbc->db, SQL, -1, &dbc->coll->insert, NULL);
	if (ret != SQLITE_OK)
		goto out_finalize_insert;

	sprintf(SQL, "DELETE FROM %s WHERE pid = ? AND cid = ? AND oid = ?;", 
		dbc->coll->name);
	ret = sqlite3_prepare(dbc->db, SQL, -1, &dbc->coll->delete, NULL);
	if (ret != SQLITE_OK)
		goto out_finalize_delete;

	sprintf(SQL, "DELETE FROM %s WHERE pid = ? AND cid = ?;", 
		dbc->coll->name);
	ret = sqlite3_prepare(dbc->db, SQL, -1, &dbc->coll->delcid, NULL);
	if (ret != SQLITE_OK)
		goto out_finalize_delcid;

	sprintf(SQL, "DELETE FROM %s WHERE pid = ? AND oid = ?;", 
		dbc->coll->name);
	ret = sqlite3_prepare(dbc->db, SQL, -1, &dbc->coll->deloid, NULL);
	if (ret != SQLITE_OK)
		goto out_finalize_deloid;

	sprintf(SQL, "SELECT COUNT (*) FROM %s WHERE pid = ? AND cid = ? "
		" LIMIT 1;", dbc->coll->name);
	ret = sqlite3_prepare(dbc->db, SQL, -1, &dbc->coll->emptycid, NULL);
	if (ret != SQLITE_OK)
		goto out_finalize_emptycid;

	sprintf(SQL, "SELECT cid FROM %s WHERE pid = ? AND oid = ? AND "
		" number = ?;", dbc->coll->name);
	ret = sqlite3_prepare(dbc->db, SQL, -1, &dbc->coll->getcid, NULL);
	if (ret != SQLITE_OK)
		goto out_finalize_getcid;

	sprintf(SQL, "SELECT oid FROM %s WHERE pid = ? AND cid = ? AND "
		" oid >= ?;", dbc->coll->name);
	ret = sqlite3_prepare(dbc->db, SQL, -1, &dbc->coll->getoids, NULL);
	if (ret != SQLITE_OK)
		goto out_finalize_getoids;

	sprintf(SQL, "INSERT INTO %s SELECT ?, ?, oid, 0 FROM %s WHERE "
		"pid = ? AND cid = ?;", dbc->coll->name, dbc->coll->name);
	ret = sqlite3_prepare(dbc->db, SQL, -1, &dbc->coll->copyoids, NULL);
	if (ret != SQLITE_OK)
		goto out_finalize_copyoids;

	ret = OSD_OK; /* success */
	goto out;

out_finalize_copyoids:
	db_sqfinalize(dbc->db, dbc->coll->copyoids, SQL);
	SQL[0] = '\0';
out_finalize_getoids:
	db_sqfinalize(dbc->db, dbc->coll->getoids, SQL);
	SQL[0] = '\0';
out_finalize_getcid:
	db_sqfinalize(dbc->db, dbc->coll->getcid, SQL);
	SQL[0] = '\0';
out_finalize_emptycid:
	db_sqfinalize(dbc->db, dbc->coll->emptycid, SQL);
	SQL[0] = '\0';
out_finalize_deloid:
	db_sqfinalize(dbc->db, dbc->coll->deloid, SQL);
	SQL[0] = '\0';
out_finalize_delcid:
	db_sqfinalize(dbc->db, dbc->coll->delcid, SQL);
	SQL[0] = '\0';
out_finalize_delete:
	db_sqfinalize(dbc->db, dbc->coll->delete, SQL);
	SQL[0] = '\0';
out_finalize_insert:
	db_sqfinalize(dbc->db, dbc->coll->insert, SQL);
	ret = -EIO;
out:
	return ret;
}


int coll_finalize(struct db_context *dbc)
{
	if (!dbc || !dbc->coll)
		return OSD_ERROR;

	/* finalize statements; ignore return values */
	sqlite3_finalize(dbc->coll->insert);
	sqlite3_finalize(dbc->coll->delete);
	sqlite3_finalize(dbc->coll->delcid);
	sqlite3_finalize(dbc->coll->deloid);
	sqlite3_finalize(dbc->coll->emptycid);
	sqlite3_finalize(dbc->coll->getcid);
	sqlite3_finalize(dbc->coll->getoids);
	sqlite3_finalize(dbc->coll->copyoids);
	free(dbc->coll->name);
	free(dbc->coll);
	dbc->coll = NULL;

	return OSD_OK;
}


const char *coll_getname(struct db_context *dbc)
{
	if (dbc == NULL || dbc->coll == NULL)
		return NULL;
	return dbc->coll->name;
}


/* 
 * @pid: partition id 
 * @cid: collection id
 * @oid: userobject id
 * @number: attribute number of cid in CAP of oid
 *
 * returns:
 * -EINVAL: invalid arg
 * OSD_ERROR: some other error
 * OSD_OK: success
 */
int coll_insert(struct db_context *dbc, uint64_t pid, uint64_t cid,
		uint64_t oid, uint32_t number)
{
	int ret = 0;

	assert(dbc && dbc->db && dbc->coll && dbc->coll->insert);

repeat:
	ret = 0;
	ret |= sqlite3_bind_int64(dbc->coll->insert, 1, pid);
	ret |= sqlite3_bind_int64(dbc->coll->insert, 2, cid);
	ret |= sqlite3_bind_int64(dbc->coll->insert, 3, oid);
	ret |= sqlite3_bind_int(dbc->coll->insert, 4, number);
	ret = db_exec_dms(dbc, dbc->coll->insert, ret, __func__);
	if (ret == OSD_REPEAT)
		goto repeat;

	return ret;
}

/* 
 * @pid: partition id 
 * @dest_cid: destination collection id (typically user tracking collection)
 * @source_cid: source collection id (whose objects will be tracked)
 *
 * returns:
 * -EINVAL: invalid arg
 * OSD_ERROR: some other error
 * OSD_OK: success
 */
int coll_copyoids(struct db_context *dbc, uint64_t pid, uint64_t dest_cid,
		  uint64_t source_cid)
{
	int ret = 0;

	assert(dbc && dbc->db && dbc->coll && dbc->coll->copyoids);

repeat:
	ret = 0;
	ret |= sqlite3_bind_int64(dbc->coll->copyoids, 1, pid);
	ret |= sqlite3_bind_int64(dbc->coll->copyoids, 2, dest_cid);
	ret |= sqlite3_bind_int64(dbc->coll->copyoids, 3, pid);
	ret |= sqlite3_bind_int64(dbc->coll->copyoids, 4, source_cid);
	ret = db_exec_dms(dbc, dbc->coll->copyoids, ret, __func__);
	if (ret == OSD_REPEAT)
		goto repeat;

	return ret;
}


/* 
 * returns:
 * -EINVAL: invalid arg
 * OSD_ERROR: some other error
 * OSD_OK: success
 */
int coll_delete(struct db_context *dbc, uint64_t pid, uint64_t cid, 
		uint64_t oid)
{
	int ret = 0;

	assert(dbc && dbc->db && dbc->coll && dbc->coll->delete);

repeat:
	ret = 0;
	ret |= sqlite3_bind_int64(dbc->coll->delete, 1, pid);
	ret |= sqlite3_bind_int64(dbc->coll->delete, 2, cid);
	ret |= sqlite3_bind_int64(dbc->coll->delete, 3, oid);
	ret = db_exec_dms(dbc, dbc->coll->delete, ret, __func__);
	if (ret == OSD_REPEAT)
		goto repeat;

	return ret;
}


/* 
 * returns:
 * -EINVAL: invalid arg
 * OSD_ERROR: some other error
 * OSD_OK: success
 */
int coll_delete_cid(struct db_context *dbc, uint64_t pid, uint64_t cid)
{
	int ret = 0;

	assert(dbc && dbc->db && dbc->coll && dbc->coll->delcid);

repeat:
	ret = 0;
	ret |= sqlite3_bind_int64(dbc->coll->delcid, 1, pid);
	ret |= sqlite3_bind_int64(dbc->coll->delcid, 2, cid);
	ret = db_exec_dms(dbc, dbc->coll->delcid, ret, __func__);
	if (ret == OSD_REPEAT)
		goto repeat;

	return ret;
}


/* 
 * returns:
 * -EINVAL: invalid arg
 * OSD_ERROR: some other error
 * OSD_OK: success
 */
int coll_delete_oid(struct db_context *dbc, uint64_t pid, uint64_t oid)
{
	int ret = 0;

	assert(dbc && dbc->db && dbc->coll && dbc->coll->deloid);

repeat:
	ret = 0;
	ret |= sqlite3_bind_int64(dbc->coll->deloid, 1, pid);
	ret |= sqlite3_bind_int64(dbc->coll->deloid, 2, oid);
	ret = db_exec_dms(dbc, dbc->coll->deloid, ret, __func__);
	if (ret == OSD_REPEAT)
		goto repeat;

	return ret;
}


/*
 * tests whether collection is empty. 
 *
 * returns:
 * -EINVAL: invalid arg, ignore value of isempty
 * OSD_ERROR: in case of other errors, ignore value of isempty
 * OSD_OK: success, isempty is set to:
 * 	==1: if collection is empty or absent 
 * 	==0: if not empty
 */
int coll_isempty_cid(struct db_context *dbc, uint64_t pid, uint64_t cid,
		     int *isempty)
{
	int ret = 0;
	int bound = 0;
	*isempty = 0;

	assert(dbc && dbc->db && dbc->coll && dbc->coll->emptycid);

repeat:
	ret = 0;
	ret |= sqlite3_bind_int64(dbc->coll->emptycid, 1, pid);
	ret |= sqlite3_bind_int64(dbc->coll->emptycid, 2, cid);
	bound = (ret == SQLITE_OK);
	if (!bound) {
		error_sql(dbc->db, "%s: bind failed", __func__);
		goto out_reset;
	}

	while ((ret = sqlite3_step(dbc->coll->emptycid)) == SQLITE_BUSY);
	if (ret == SQLITE_ROW)
		*isempty = (0 == sqlite3_column_int(dbc->coll->emptycid, 0));

out_reset:
	ret = db_reset_stmt(dbc, dbc->coll->emptycid, bound, __func__);
	if (ret == OSD_REPEAT)
		goto repeat;
out:
	return ret;
}


/*
 * returns:
 * -EINVAL: invalid arg, cid is not set
 * OSD_ERROR: in case of any error, cid is not set
 * OSD_OK: success, cid is set to proper collection id
 */
int coll_get_cid(struct db_context *dbc, uint64_t pid, uint64_t oid, 
		 uint32_t number, uint64_t *cid)
{
	int ret = 0;
	int bound = 0;

	assert(dbc && dbc->db && dbc->coll && dbc->coll->getcid);

repeat:
	ret = 0;
	ret |= sqlite3_bind_int64(dbc->coll->getcid, 1, pid);
	ret |= sqlite3_bind_int64(dbc->coll->getcid, 2, oid);
	ret |= sqlite3_bind_int64(dbc->coll->getcid, 3, number);
	bound = (ret == SQLITE_OK);
	if (!bound) {
		error_sql(dbc->db, "%s: bind failed", __func__);
		goto out_reset;
	}

	while ((ret = sqlite3_step(dbc->coll->getcid)) == SQLITE_BUSY);
	if (ret == SQLITE_ROW)
		*cid = sqlite3_column_int64(dbc->coll->getcid, 0);

out_reset:
	ret = db_reset_stmt(dbc, dbc->coll->getcid, bound, __func__);
	if (ret == OSD_REPEAT)
		goto repeat;
out:
	return ret;
}


/*
 * returns:
 * -EINVAL: invalid arg
 * OSD_ERROR: other errors
 * OSD_OK: success, oids copied into outbuf, cont_id set if necessary
 */
int coll_get_oids_in_cid(struct db_context *dbc, uint64_t pid, uint64_t cid, 
		       uint64_t initial_oid, uint64_t alloc_len, 
		       uint8_t *outdata, uint64_t *used_outlen,
		       uint64_t *add_len, uint64_t *cont_id)
{
	int ret = 0;
	uint64_t len = 0;
	sqlite3_stmt *stmt = NULL;

	assert(dbc && dbc->db && dbc->coll && dbc->coll->getoids);

repeat:
	ret = 0;
	stmt = dbc->coll->getoids;
	ret |= sqlite3_bind_int64(stmt, 1, pid);
	ret |= sqlite3_bind_int64(stmt, 2, cid);
	ret |= sqlite3_bind_int64(stmt, 3, initial_oid);
	ret = db_exec_id_rtrvl_stmt(dbc, stmt, ret, __func__, alloc_len,
				    outdata, used_outlen, add_len, cont_id);
	if (ret == OSD_REPEAT)
		goto repeat;

	return ret;
}

/*
 * Collection Attributes Page (CAP) of a userobject stores its membership in 
 * collections osd2r01 Sec 7.1.2.19.
 */
int coll_get_cap(sqlite3 *db, uint64_t pid, uint64_t oid, void *outbuf, 
		 uint64_t outlen, uint8_t listfmt, uint32_t *used_outlen)
{
	return OSD_ERROR;
}

