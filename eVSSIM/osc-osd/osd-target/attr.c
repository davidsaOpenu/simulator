/*
 * Attributes.
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
#include "attr.h"
#include "osd-util/osd-util.h"
#include "list-entry.h"

/* attr table stores all the attributes of all the objects in the OSD */

/* 40 bytes including terminating NUL */
static const char unid_page[ATTR_PAGE_ID_LEN] = 
"        unidentified attributes page   ";

static const char *attr_tab_name = "attr";
struct attr_tab {
	char *name;             /* name of the table */
	sqlite3_stmt *setattr;  /* set an attr by inserting row */
	sqlite3_stmt *delattr;  /* delete an attr */
	sqlite3_stmt *delall;   /* delete all attr for an object */
	sqlite3_stmt *getattr;  /* get an attr */
	sqlite3_stmt *getval;   /* get attribute value */
	sqlite3_stmt *pgaslst;  /* get page as list */
	sqlite3_stmt *forallpg; /* for all pages get an attribute */
	sqlite3_stmt *getall;   /* get all attributes of an object */
	sqlite3_stmt *dirpage;  /* get directory page of object's attr */
};


/*
 * returns:
 * -ENOMEM: out of memory
 * -EINVAL: invalid args
 * -EIO: if any prepare statement fails
 *  OSD_OK: success
 */
int attr_initialize(struct db_context *dbc)
{
	int ret = 0;
	int sqlret = 0;
	char SQL[MAXSQLEN];

	if (dbc == NULL || dbc->db == NULL) {
		ret = -EINVAL;
		goto out;
	}

	if (dbc->attr != NULL) {
		if (strcmp(dbc->attr->name, attr_tab_name) != 0) {
			ret = -EINVAL;
			goto out;
		} else {
			attr_finalize(dbc);
		}
	}

	dbc->attr = Calloc(1, sizeof(*dbc->attr));
	if (!dbc->attr) {
		ret = -ENOMEM;
		goto out;
	}
	
	dbc->attr->name = strdup(attr_tab_name); 
	if (!dbc->attr->name) {
		ret = -ENOMEM;
		goto out;
	}

	sprintf(SQL, "INSERT OR REPLACE INTO %s VALUES (?, ?, ?, ?, ?);", 
		dbc->attr->name);
	ret = sqlite3_prepare(dbc->db, SQL, -1, &dbc->attr->setattr, NULL);
	if (ret != SQLITE_OK)
		goto out_finalize_setattr;

	sprintf(SQL, "DELETE FROM %s WHERE pid = ? AND oid = ? AND page = ? "
		" AND number = ?;", dbc->attr->name);
	ret = sqlite3_prepare(dbc->db, SQL, -1, &dbc->attr->delattr, NULL);
	if (ret != SQLITE_OK)
		goto out_finalize_delattr;

	sprintf(SQL, "DELETE FROM %s WHERE pid = ? AND oid = ?;",
		dbc->attr->name);
	ret = sqlite3_prepare(dbc->db, SQL, -1, &dbc->attr->delall, NULL);
	if (ret != SQLITE_OK)
		goto out_finalize_delall;

	sprintf(SQL, "SELECT page, number, value FROM %s WHERE pid = ? AND "
		" oid = ? AND page = ? AND number = ?;", dbc->attr->name);
	ret = sqlite3_prepare(dbc->db, SQL, -1, &dbc->attr->getattr, NULL);
	if (ret != SQLITE_OK)
		goto out_finalize_getattr;

	sprintf(SQL, "SELECT value FROM %s WHERE pid = ? AND oid = ? AND "
		" page = ? AND number = ?;", dbc->attr->name);
	ret = sqlite3_prepare(dbc->db, SQL, -1, &dbc->attr->getval, NULL);
	if (ret != SQLITE_OK)
		goto out_finalize_getval;

	sprintf(SQL, "SELECT page, number, value FROM %s WHERE pid = ? AND "
		" oid = ? AND page = ?;", dbc->attr->name);
	ret = sqlite3_prepare(dbc->db, SQL, -1, &dbc->attr->pgaslst, NULL);
	if (ret != SQLITE_OK)
		goto out_finalize_pgaslst;

	sprintf(SQL, "SELECT page, number, value FROM %s WHERE pid = ? AND "
		" oid = ? AND number = ?;", dbc->attr->name);
	ret = sqlite3_prepare(dbc->db, SQL, -1, &dbc->attr->forallpg, NULL);
	if (ret != SQLITE_OK)
		goto out_finalize_forallpg;

	sprintf(SQL, "SELECT page, number, value FROM %s WHERE pid = ? AND "
		"oid = ?;", dbc->attr->name);
	ret = sqlite3_prepare(dbc->db, SQL, -1, &dbc->attr->getall, NULL);
	if (ret != SQLITE_OK)
		goto out_finalize_getall;

	/* 
	 * We need to compute the directory page since we don't maintain any.
	 * This SQL statement is inefficient. Its complexity is O(N) where
	 * 'N' is the total number of defined attributes of the object.
	 * Further the statement executes 3 iterations over all the
	 * attributes of the object, hence the constant term is also
	 * significant. There is some advantage in adding an index on
	 * (pid,oid,number) but that will hit performance of insertions and
	 * deletions which are more critical than dirpage.
	 */
	sprintf(SQL, 
		" SELECT page, @uip  FROM %s"
		"   WHERE pid = @pid AND oid = @oid GROUP BY page "
		" EXCEPT "
		" SELECT page, @uip  FROM attr "
		"   WHERE pid = @pid AND oid = @oid AND number = 0 "
		" UNION ALL "
		" SELECT page, value FROM attr "
		"   WHERE pid = @pid AND oid = @oid AND number = 0;", 
		dbc->attr->name);
	ret = sqlite3_prepare(dbc->db, SQL, -1, &dbc->attr->dirpage, NULL);
	if (ret != SQLITE_OK)
		goto out_finalize_dirpage;

	ret = OSD_OK; /* success */
	goto out;

out_finalize_dirpage:
	db_sqfinalize(dbc->db, dbc->attr->dirpage, SQL);
	SQL[0] = '\0';
out_finalize_getall:
	db_sqfinalize(dbc->db, dbc->attr->getall, SQL);
	SQL[0] = '\0';
out_finalize_forallpg:
	db_sqfinalize(dbc->db, dbc->attr->forallpg, SQL);
	SQL[0] = '\0';
out_finalize_pgaslst:
	db_sqfinalize(dbc->db, dbc->attr->pgaslst, SQL);
	SQL[0] = '\0';
out_finalize_getval:
	db_sqfinalize(dbc->db, dbc->attr->getval, SQL);
	SQL[0] = '\0';
out_finalize_getattr:
	db_sqfinalize(dbc->db, dbc->attr->getattr, SQL);
	SQL[0] = '\0';
out_finalize_delall:
	db_sqfinalize(dbc->db, dbc->attr->delall, SQL);
	SQL[0] = '\0';
out_finalize_delattr:
	db_sqfinalize(dbc->db, dbc->attr->delattr, SQL);
	SQL[0] = '\0';
out_finalize_setattr:
	db_sqfinalize(dbc->db, dbc->attr->setattr, SQL);
	ret = -EIO;
out:
	return ret;
}


int attr_finalize(struct db_context *dbc)
{
	if (!dbc || !dbc->attr)
		return OSD_ERROR;

	/* finalize statements; ignore return values */
	sqlite3_finalize(dbc->attr->setattr);
	sqlite3_finalize(dbc->attr->delattr);
	sqlite3_finalize(dbc->attr->delall);
	sqlite3_finalize(dbc->attr->getattr);
	sqlite3_finalize(dbc->attr->getval);
	sqlite3_finalize(dbc->attr->pgaslst);
	sqlite3_finalize(dbc->attr->forallpg);
	sqlite3_finalize(dbc->attr->getall);
	sqlite3_finalize(dbc->attr->dirpage);
	free(dbc->attr->name);
	free(dbc->attr);
	dbc->attr = NULL;

	return OSD_OK;
}


const char *attr_getname(struct db_context *dbc)
{
	if (dbc == NULL || dbc->attr == NULL)
		return NULL;
	return dbc->attr->name;
}


/*
 * Note: Current SQLITE INSERT syntax does not support bulk inserts in a
 * single INSERT SQL statement. Therefore this function needs to be called
 * for each table insert.
 *
 * returns:
 * -EINVAL: invalid arg
 * OSD_ERROR: some other error
 * OSD_OK: success
 */
int attr_set_attr(struct db_context *dbc, uint64_t pid, uint64_t oid, 
		  uint32_t page, uint32_t number, const void *val, 
		  uint16_t len)
{
	int ret = 0;
	sqlite3_stmt *stmt = NULL;

	assert(dbc && dbc->db && dbc->attr && dbc->attr->setattr);

repeat:
	ret = 0;
	stmt = dbc->attr->setattr;
	ret |= sqlite3_bind_int64(stmt, 1, pid);
	ret |= sqlite3_bind_int64(stmt, 2, oid);
	ret |= sqlite3_bind_int(stmt, 3, page);
	ret |= sqlite3_bind_int(stmt, 4, number);
	ret |= sqlite3_bind_blob(stmt, 5, val, len, SQLITE_TRANSIENT);
	ret = db_exec_dms(dbc, stmt, ret, __func__);
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
int attr_delete_attr(struct db_context *dbc, uint64_t pid, uint64_t oid, 
		     uint32_t page, uint32_t number)
{
	int ret = 0;
	sqlite3_stmt *stmt = NULL;

	assert(dbc && dbc->db && dbc->attr && dbc->attr->delattr);

repeat:
	ret = 0;
	stmt = dbc->attr->delattr;
	ret |= sqlite3_bind_int64(stmt, 1, pid);
	ret |= sqlite3_bind_int64(stmt, 2, oid);
	ret |= sqlite3_bind_int(stmt, 3, page);
	ret |= sqlite3_bind_int(stmt, 4, number);
	ret = db_exec_dms(dbc, stmt, ret, __func__);
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
int attr_delete_all(struct db_context *dbc, uint64_t pid, uint64_t oid)
{
	int ret = 0;

	assert(dbc && dbc->db && dbc->attr && dbc->attr->delall);

repeat:
	ret = 0;
	ret |= sqlite3_bind_int64(dbc->attr->delall, 1, pid);
	ret |= sqlite3_bind_int64(dbc->attr->delall, 2, oid);
	ret = db_exec_dms(dbc, dbc->attr->delall, ret, __func__);
	if (ret == OSD_REPEAT)
		goto repeat;

	return ret;
}


/* 
 * Gather the results into list_entry format. Each row has page, number, len,
 * value. Look at queries in attr_get_attr attr_get_attr_page.  See page 163.
 *
 * -EINVAL: invalid argument
 * -EOVERFLOW: error, if not enough room to even start the entry
 * >0: success. returns number of bytes copied into outbuf.
 */
static int attr_gather_attr(sqlite3_stmt *stmt, void *buf, uint32_t buflen,
			    uint64_t oid, uint8_t listfmt)
{
	uint32_t page = sqlite3_column_int(stmt, 0);
	uint32_t number = sqlite3_column_int(stmt, 1);
	uint16_t len = sqlite3_column_bytes(stmt, 2);
	const void *val = sqlite3_column_blob(stmt, 2);

	if (listfmt == RTRVD_SET_ATTR_LIST) {
		return le_pack_attr(buf, buflen, page, number, len, val);
	} else if (listfmt == RTRVD_CREATE_MULTIOBJ_LIST) {
		return le_multiobj_pack_attr(buf, buflen, oid, page, number,
					     len, val);
	} else {
		return -EINVAL;
	}
}


/*
 * Gather attr val. Conservative implementation: if not enough space for the
 * attr then return err.
 * -EINVAL: error
 * >0: success. returns number of bytes copied into buf.
 */
static int attr_gather_val(sqlite3_stmt *stmt, void *buf, uint32_t buflen)
{
	uint16_t len = sqlite3_column_bytes(stmt, 0);
	const void *val = sqlite3_column_blob(stmt, 0);

	if (buflen < len)
		return -EINVAL;

	memcpy(buf, val, len);
	return len;
}


/*
 * packs contents of an attribute of dir page. Note that even though the
 * query selects 'page', the resultant rows are actually rows 'numbers'
 * within directory page.
 * 
 * returns:
 * -EINVAL: invalid argument, or length(attribute value) != 40
 * -EOVERFLOW: outlen too small
 * >0: success, number of bytes copied into outdata
 */
static int attr_gather_dir_page(sqlite3_stmt *stmt, uint32_t outlen,
				void *outdata, uint64_t oid, uint32_t page,
				uint8_t listfmt)
{
	uint32_t number = sqlite3_column_int(stmt, 0);
	uint16_t len = sqlite3_column_bytes(stmt, 1);
	const void *val = sqlite3_column_blob(stmt, 1);

	if (len != ATTR_PAGE_ID_LEN) 
		return -EINVAL;

	if (listfmt == RTRVD_SET_ATTR_LIST) {
		return le_pack_attr(outdata, outlen, page, number, len, val);
	} else if (listfmt == RTRVD_CREATE_MULTIOBJ_LIST) {
		return le_multiobj_pack_attr(outdata, outlen, oid, page, 
					     number, len, val);
	} else {
		return -EINVAL;
	}
}


/*
 * returns:
 * -EINVAL: invalid arg or misaligned buffer
 * -ENOENT: empty result set
 * OSD_ERROR: some other error
 * OSD_OK: success, used_outlen modified
 */
static int exec_attr_rtrvl_stmt(struct db_context *dbc, sqlite3_stmt *stmt,
				int ret, const char *func, uint64_t oid, 
				uint64_t page, int rtrvl_type, uint64_t outlen,
				uint8_t *outdata, uint8_t listfmt,
				uint32_t *used_outlen)
{
	uint32_t len = 0;
	uint8_t found = 0;
	uint8_t bound = (ret == SQLITE_OK);
	uint8_t inval = 0;

	if (!bound) {
		error_sql(dbc->db, "%s: bind failed", func);
		goto out_reset;
	}

	*used_outlen = 0;
	while (1) {
		ret = sqlite3_step(stmt);
		if (ret == SQLITE_ROW) {
			if (rtrvl_type == GATHER_VAL) {
				ret = attr_gather_val(stmt, outdata, outlen);
			} else if (rtrvl_type == GATHER_ATTR) {
				ret = attr_gather_attr(stmt, outdata, outlen, 
						       oid, listfmt);
			} else if (rtrvl_type == GATHER_DIR_PAGE) {
				ret = attr_gather_dir_page(stmt, outlen,
							   outdata, oid, page,
							   listfmt);
			} else {
				ret = -EINVAL;
			}
			if (ret > 0) {
				len += ret;
				outlen -= ret;
				outdata += ret;
				if (!found)
					found = 1;
			} else {
				if (ret == -EINVAL)
					inval = 1;
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
	if (inval) {
		ret = -EINVAL;
	} else if (ret == OSD_OK) {
		*used_outlen = len;
		if (!found)
			ret = -ENOENT;
	}
	return ret;
}


/*
 * get one attribute in list format.
 *
 * -EINVAL: invalid arg, ignore used_len 
 * -ENOENT: error, attribute not found
 * OSD_ERROR: some other error
 * OSD_OK: success, used_outlen modified
 */
int attr_get_attr(struct db_context *dbc, uint64_t pid, uint64_t oid,
		  uint32_t page, uint32_t number, uint64_t outlen,
		  void *outdata, uint8_t listfmt, uint32_t *used_outlen)
{
	int ret = 0;
	sqlite3_stmt *stmt = NULL;

	assert(dbc && dbc->db && dbc->attr && dbc->attr->getattr);

repeat:
	ret = 0;
	stmt = dbc->attr->getattr;
	ret |= sqlite3_bind_int64(stmt, 1, pid);
	ret |= sqlite3_bind_int64(stmt, 2, oid);
	ret |= sqlite3_bind_int(stmt, 3, page);
	ret |= sqlite3_bind_int(stmt, 4, number);
	ret = exec_attr_rtrvl_stmt(dbc, stmt, ret, __func__, oid, 0, 
				   GATHER_ATTR, outlen, outdata, listfmt,
				   used_outlen); 
	if (ret == OSD_REPEAT) {
		goto repeat;
	} else if (ret == -ENOENT) {
		osd_debug("%s: attr (%llu %llu %u %u) not found!", __func__, 
			  llu(pid), llu(oid), page, number);
	}

	return ret;
}


/*
 * get one attribute value.
 *
 * -EINVAL: invalid arg, ignore used_len 
 * -ENOENT: error, attribute not found
 * OSD_ERROR: some other error
 * OSD_OK: success, used_outlen modified
 */
int attr_get_val(struct db_context *dbc, uint64_t pid, uint64_t oid,
		 uint32_t page, uint32_t number, uint64_t outlen,
		 void *outdata, uint32_t *used_outlen)
{
	int ret = 0;
	sqlite3_stmt *stmt = NULL;

	assert(dbc && dbc->db && dbc->attr && dbc->attr->getval);

repeat:
	ret = 0;
	stmt = dbc->attr->getval;
	ret |= sqlite3_bind_int64(stmt, 1, pid);
	ret |= sqlite3_bind_int64(stmt, 2, oid);
	ret |= sqlite3_bind_int(stmt, 3, page);
	ret |= sqlite3_bind_int(stmt, 4, number);
	ret = exec_attr_rtrvl_stmt(dbc, stmt, ret, __func__, oid, 0, 
				   GATHER_VAL, outlen, outdata, 0,
				   used_outlen); 
	if (ret == OSD_REPEAT) {
		goto repeat;
	} else if (ret == -ENOENT) {
		osd_debug("%s: attr (%llu %llu %u %u) not found!", __func__, 
			  llu(pid), llu(oid), page, number);
	}

	return ret;
}

/*
 * get one page in list format
 *
 * XXX:SD If the page is defined and we don't have name for the page (attr
 * num == 0), then we don't return its name. That is a bug
 *
 * -EINVAL: invalid arg, ignore used_len 
 * -ENOENT: error, attribute not found
 * OSD_ERROR: some other error
 * OSD_OK: success, used_outlen modified
 */
int attr_get_page_as_list(struct db_context *dbc, uint64_t pid, uint64_t oid,
			  uint32_t page, uint64_t outlen, void *outdata,
			  uint8_t listfmt, uint32_t *used_outlen)
{
	int ret = 0;
	sqlite3_stmt *stmt = NULL;

	assert(dbc && dbc->db && dbc->attr && dbc->attr->pgaslst);

repeat:
	ret = 0;
	stmt = dbc->attr->pgaslst;
	ret |= sqlite3_bind_int64(stmt, 1, pid);
	ret |= sqlite3_bind_int64(stmt, 2, oid);
	ret |= sqlite3_bind_int(stmt, 3, page);
	ret = exec_attr_rtrvl_stmt(dbc, stmt, ret, __func__, oid, 0, 
				   GATHER_ATTR, outlen, outdata, listfmt,
				   used_outlen); 
	if (ret == OSD_REPEAT) {
		goto repeat;
	} else if (ret == -ENOENT) {
		osd_debug("%s: attr (%llu %llu %u *) not found!", __func__, 
			  llu(pid), llu(oid), page);
	}

	return ret;
}


/*
 * for each defined page of an object get attribute with specified number
 *
 * XXX:SD If the page is defined and we don't have name for the page (attr
 * num == 0), then we don't return its name. That is a bug
 *
 * -EINVAL: invalid arg, ignore used_len 
 * -ENOENT: error, attribute not found
 * OSD_ERROR: some other error
 * OSD_OK: success, used_outlen modified
 */
int attr_get_for_all_pages(struct db_context *dbc, uint64_t pid, uint64_t oid, 
			   uint32_t number, uint64_t outlen, void *outdata,
			   uint8_t listfmt, uint32_t *used_outlen)
{
	int ret = 0;
	sqlite3_stmt *stmt = NULL;

	assert(dbc && dbc->db && dbc->attr && dbc->attr->forallpg);

repeat:
	ret = 0;
	stmt = dbc->attr->forallpg;
	ret |= sqlite3_bind_int64(stmt, 1, pid);
	ret |= sqlite3_bind_int64(stmt, 2, oid);
	ret |= sqlite3_bind_int(stmt, 3, number);
	ret = exec_attr_rtrvl_stmt(dbc, stmt, ret, __func__, oid, 0,
				   GATHER_ATTR, outlen, outdata, listfmt,
				   used_outlen);
	if (ret == OSD_REPEAT) {
		goto repeat;
	} else if (ret == -ENOENT) {
		osd_debug("%s: attr (%llu %llu * %u) not found!", __func__, 
			  llu(pid), llu(oid), number);
	}

	return ret;
}


/*
 * get all attributes for an object in a list format
 *
 * XXX:SD If the page is defined and we don't have name for the page (attr
 * num == 0), then we don't return its name. That is a bug
 *
 * returns: 
 * -EINVAL: invalid arg, ignore used_len 
 * -ENOENT: error, attribute not found
 * OSD_ERROR: some other error
 * OSD_OK: success, used_outlen modified
 */
int attr_get_all_attrs(struct db_context *dbc, uint64_t pid, uint64_t oid,
		       uint64_t outlen, void *outdata, uint8_t listfmt, 
		       uint32_t *used_outlen)
{
	int ret = 0;
	sqlite3_stmt *stmt = NULL;

	assert(dbc && dbc->db && dbc->attr && dbc->attr->getall);

repeat:
	ret = 0;
	stmt = dbc->attr->getall;
	ret |= sqlite3_bind_int64(stmt, 1, pid);
	ret |= sqlite3_bind_int64(stmt, 2, oid);
	ret = exec_attr_rtrvl_stmt(dbc, stmt, ret, __func__, oid, 0,
				   GATHER_ATTR, outlen, outdata, listfmt,
				   used_outlen);
	if (ret == OSD_REPEAT) {
		goto repeat;
	} else if (ret == -ENOENT) {
		osd_debug("%s: attr (%llu %llu * *) not found!", __func__, 
			  llu(pid), llu(oid));
	}

	return ret;
}


/*
 * get the directory page of the object.
 *
 * returns:
 * -EINVAL: invalid arg, ignore used_len 
 * -ENOENT: error, attribute not found
 * OSD_ERROR: some other error
 * OSD_OK: success, used_outlen modified
 */
int attr_get_dir_page(struct db_context *dbc, uint64_t pid, uint64_t oid, 
		      uint32_t page, uint64_t outlen, void *outdata,
		      uint8_t listfmt, uint32_t *used_outlen)
{
	int ret = 0;
	sqlite3_stmt *stmt = NULL;

	assert(dbc && dbc->db && dbc->attr && dbc->attr->dirpage);

	if (page != USEROBJECT_DIR_PG && page != COLLECTION_DIR_PG &&
	    page != PARTITION_DIR_PG && page != ROOT_DIR_PG)
		return -EINVAL;
repeat:
	ret = 0;
	stmt = dbc->attr->dirpage;
	ret |= sqlite3_bind_blob(stmt, 1, unid_page, sizeof(unid_page), 
				 SQLITE_TRANSIENT);
	ret |= sqlite3_bind_int64(stmt, 2, pid);
	ret |= sqlite3_bind_int64(stmt, 3, oid);
	ret = exec_attr_rtrvl_stmt(dbc, stmt, ret, __func__, oid, page,
				   GATHER_DIR_PAGE, outlen, outdata, listfmt,
				   used_outlen);
	if (ret == OSD_REPEAT) {
		goto repeat;
	} else if (ret == -ENOENT) {
		osd_debug("%s: dir page not found!", __func__);
	}

	return ret;
}

