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
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/types.h>
#include <dirent.h>
#include <assert.h>

#include <linux/fs.h>

#include "osd.h"
#include "target-sense.h"
#include "db.h"
#include "attr.h"
#include "osd-util/osd-util.h"
#include "obj.h"
#include "coll.h"
#include "mtq.h"
#include "osd-util/osd-sense.h"
#include "list-entry.h"

#define min(x,y) ({ \
	typeof(x) _x = (x);	\
	typeof(y) _y = (y);	\
	(void) (&_x == &_y);		\
	_x < _y ? _x : _y; })

#ifdef __MAKE_BSD_BUILD__
static int os_sync_file_range(int fd, __off64_t offset, __off64_t bytes,
			unsigned int flags)
{
	return fsync(fd);
}
#endif

struct incits_page_id {
	const char root_dir_page[40];
	const char root_info_page[40];
	const char root_quota_page[40];
	const char root_tmstmp_page[40];
	const char root_policy_page[40];
	const char part_dir_page[40];
	const char part_info_page[40];
	const char coll_dir_page[40];
	const char coll_info_page[40];
	const char user_dir_page[40];
	const char user_info_page[40];
	const char user_tmstmp_page[40];
	const char user_atomic_page[40];
};

static const struct incits_page_id incits = {
	.root_dir_page = 	"INCITS  T10 Root Directory             ",
        .root_info_page =       "INCITS  T10 Root Information           ",
        .root_quota_page =      "INCITS  T10 Root Quotas                ",
        .root_tmstmp_page =     "INCITS  T10 Root Timestamps            ",
        .root_policy_page =     "INCITS  T10 Root Policy/Security       ",
        .part_dir_page =        "INCITS  T10 Partition Directory        ",
        .part_info_page =       "INCITS  T10 Partition Information      ",
        .coll_dir_page =        "INCITS  T10 Collection Directory       ",
        .coll_info_page =       "INCITS  T10 Collection Information     ",
        .user_dir_page =        "INCITS  T10 User Object Directory      ",
        .user_info_page =       "INCITS  T10 User Object Information    ",
	.user_tmstmp_page =     "INCITS  T10 User Object Timestamps     ",
	.user_atomic_page = 	"INCITS  T10 User Atomics               ",
};

static const char *md = "md";
static const char *dbname = "osd.db";
static const char *dfiles = "dfiles";
static const char *stranded = "stranded";

static inline uint8_t get_obj_type(struct osd_device *osd,
				   uint64_t pid, uint64_t oid)
{
	int ret = 0;
	uint8_t obj_type = ILLEGAL_OBJ;

	if (pid == ROOT_PID && oid == ROOT_OID) {
		return ROOT;
	} else if (pid >= PARTITION_PID_LB && oid == PARTITION_OID) {
		return PARTITION;
	} else if (pid >= OBJECT_PID_LB && oid >= OBJECT_OID_LB) {
		ret = obj_get_type(osd->dbc, pid, oid, &obj_type);
		if (ret == OSD_OK)
			return obj_type;
	}
	return ILLEGAL_OBJ;
}

static inline int get_rel_page(uint8_t obj_type, uint32_t page)
{
	if (obj_type == ROOT)
		return (page - ROOT_PG);
	else if (obj_type == PARTITION)
		return (page - PARTITION_PG);
	else if (obj_type == COLLECTION)
		return (page - COLLECTION_PG);
	else if (obj_type == USEROBJECT)
		return (page - USEROBJECT_PG);
	else
		return -1;
}

static inline int check_valid_pg(uint8_t obj_type, uint32_t page)
{
	switch (obj_type) {
	case USEROBJECT:
		return (page < PARTITION_PG);
	case COLLECTION:
		return (COLLECTION_PG <= page && page < ROOT_PG);
	case PARTITION:
		return (PARTITION_PG <= page && page < COLLECTION_PG);
	case ROOT:
		return (ROOT_PG <= page && page < RESERVED_PG);
	default:
		return false;
	}
}

static int issettable_page(uint8_t obj_type, uint32_t page)
{
	int rel_page = 0;
	int valid_pg = check_valid_pg(obj_type, page);

	if (!valid_pg) {
		if (page >= ANY_PG)
			return true;
		else
			return false;
	}

	rel_page = get_rel_page(obj_type, page);
	if ((STD_PG_LB <= rel_page && rel_page <= STD_PG_UB) ||
	    (LUN_PG_LB <= rel_page && rel_page <= LUN_PG_UB))
		return true;
	else
		return false;
}

static int isgettable_page(uint8_t obj_type, uint32_t page)
{
	int rel_page = 0;
	int valid_pg = check_valid_pg(obj_type, page);

	if (!valid_pg) {
		if (page >= ANY_PG)
			return true;
		else
			return false;
	}

	rel_page = get_rel_page(obj_type, page);
	if ((RSRV_PG_LB <= rel_page && rel_page <= RSRV_PG_UB) ||
	    rel_page > VEND_PG_UB)
		return false;
	else
		return true;

}

static int create_dir(const char *dirname)
{
	int ret = 0;
	struct stat sb;

	ret = stat(dirname, &sb);
	if (ret == 0) {
		if (!S_ISDIR(sb.st_mode)) {
			osd_error("%s: dirname %s not a directory", __func__,
				  dirname);
			return -ENOTDIR;
		}
	} else {
		if (errno != ENOENT) {
			osd_error("%s: stat dirname %s", __func__, dirname);
			return -ENOTDIR;
		}

		/* create dir*/
		ret = mkdir(dirname, 0777);
		if (ret < 0) {
			osd_error("%s: create dirname %s", __func__, dirname);
			return ret;
		}
	}

	return 0;
}

/* This function recursively empties the directories */
static int empty_dir(const char *dirname)
{
	int ret = 0;
	char path[MAXNAMELEN];
	DIR *dir = NULL;
	struct dirent *ent = NULL;

	if ((dir = opendir(dirname)) == NULL)
		return -1;

	while ((ent = readdir(dir)) != NULL) {
		if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, ".."))
			continue;
		sprintf(path, "%s/%s", dirname, ent->d_name);
		if (ent->d_type == DT_DIR) {
			ret = empty_dir(path);
		} else {
			ret = unlink(path);
		}
		if (ret != 0)
			return -1;
	}

	closedir(dir);

	ret = rmdir(dirname);
	if (ret != 0)
		return -1;

	return 0;
}

static inline void get_dfile_name(char *path, const char *root,
				  uint64_t pid, uint64_t oid)
{
#ifdef PVFS_OSD_INTEGRATED
	/* go look in PVFS bstreams for file data (eventually) */
	sprintf(path, "%s/%08llx/bstreams/%.8llu/%08llx.bstream", root,
	        llu(pid), llu(oid % 64), llu(oid));
	printf("root = %s collid = 0x%llx\n", root, llu(pid));
#elif defined (__PANASAS_OSDSIM__)
	if (!oid)
		sprintf(path, "%s/%s/%llu", root, dfiles, llu(pid));
	else
		sprintf(path, "%s/%s/%llu/%llu/data", root, dfiles,
			llu(pid), llu(oid));
#elif defined (__PANASAS_OSD__)
	if (!oid)
		sprintf(path, "%s/%s/%llu", root, dfiles, llu(pid));
	else
		sprintf(path, "%s/%s/%llu/%llu", root, dfiles,
			llu(pid), llu(oid));
#else
	if (!oid)
		sprintf(path, "%s/%s/%02x", root, dfiles,
			(uint8_t)(oid & 0xFFUL));
	else
		sprintf(path, "%s/%s/%02x/%llx.%llx", root, dfiles,
			(uint8_t)(oid & 0xFFUL), llu(pid), llu(oid));
#endif
}

static inline void get_dbname(char *path, const char *root)
{
	sprintf(path, "%s/%s/%s", root, md, dbname);
}

static inline void fill_ccap(struct cur_cmd_attr_pg *ccap, uint8_t *ricv,
			     uint8_t obj_type, uint64_t pid, uint64_t oid,
			     uint64_t append_off)
{
	memset(ccap, 0, sizeof(*ccap));
	if (ricv)
		memcpy(ccap->ricv, ricv, sizeof(ccap->ricv));
	ccap->obj_type = obj_type;
	ccap->pid = pid;
	ccap->oid = oid;
	ccap->append_off = append_off;
}

/*
 * Fill current command attributes page  in retrieved page format (osd2r00
 * Sec 7.1.2.24).
 *
 * returns:
 * -EINVAL: generic error
 * ==0: OSD_OK (success), used_outlen stores consumed outbuf len
 */
static int get_ccap(struct osd_device *osd, void *outbuf, uint64_t outlen,
		    uint32_t *used_outlen)
{
	int ret = 0;
	uint8_t *cp = outbuf;

	assert(osd && outbuf && used_outlen);

	/*
	 * XXX:SD we are treating the case of len < CCAP_TOTAL_LEN as
	 * overflow error.
	 * overflow is not considered error. osd2r00 sec 5.2.2.2
	 */
	if (outlen < CCAP_TOTAL_LEN) {
		*used_outlen = 0;
		goto out;
	}

	memset(cp, 0, CCAP_TOTAL_LEN);
	set_htonl(&cp[0], CUR_CMD_ATTR_PG);
	set_htonl(&cp[4], CCAP_TOTAL_LEN - 8);
	memcpy(&cp[CCAP_RICV_OFF], osd->ccap.ricv, sizeof(osd->ccap.ricv));
	cp[CCAP_OBJT_OFF] = osd->ccap.obj_type;
	set_htonll(&cp[CCAP_PID_OFF], osd->ccap.pid);
	set_htonll(&cp[CCAP_OID_OFF], osd->ccap.oid);
	set_htonll(&cp[CCAP_APPADDR_OFF], osd->ccap.append_off);
	*used_outlen = CCAP_TOTAL_LEN;

out:
	return OSD_OK;
}

/*
 * Get current command attributes page (osd2r00 Sec 7.1.2.24) attributes.
 *
 * NOTE: since RTRVD_CREATE_MULTIOBJ_LIST listfmt is set only when multiple
 * objects are created, and CCAP has room for only one (pid, oid), the
 * retrieved attributes are always in RTRVD_SET_ATTR_LIST format described in
 * osd2r00 Sec 7.1.3.3
 *
 * returns:
 * -EINVAL: if error, used_outlen not modified
 * ==0: OSD_OK (success), used_outlen stores consumed outbuf len
 */
static int get_ccap_aslist(struct osd_device *osd, uint32_t number,
			   void *outbuf, uint64_t outlen,
			   uint32_t *used_outlen)
{
	int ret = 0;
	uint16_t len = 0;
	char name[ATTR_PAGE_ID_LEN] = {'\0'};
	void *val = NULL;
	uint8_t *cp = outbuf;
	uint8_t ll[8];

	assert(osd && outbuf && used_outlen);

	switch (number) {
	case 0:
		len = ATTR_PAGE_ID_LEN;
		sprintf(name, "INCITS  T10 Current Command");
		val = name;
		break;
	case CCAP_RICV:
		len = CCAP_RICV_LEN;
		val = osd->ccap.ricv;
		break;
	case CCAP_OBJT:
		len = CCAP_OBJT_LEN;
		val = &osd->ccap.obj_type;
		break;
	case CCAP_PID:
		set_htonll(ll, osd->ccap.pid);
		len = CCAP_PID_LEN;
		val = ll;
		break;
	case CCAP_OID:
		set_htonll(ll, osd->ccap.oid);
		len = CCAP_OID_LEN;
		val = ll;
		break;
	case CCAP_APPADDR:
		set_htonll(ll, osd->ccap.append_off);
		len = CCAP_APPADDR_LEN;
		val = ll;
		break;
	default:
		return OSD_ERROR;
	}

	ret = le_pack_attr(outbuf, outlen, CUR_CMD_ATTR_PG, number, len, val);
	assert(ret == -EINVAL || ret == -EOVERFLOW || ret > 0);
	if (ret == -EOVERFLOW)
		*used_outlen = 0;
	else if (ret > 0)
		*used_outlen = ret;
	else
		return ret;

	return OSD_OK;
}

static inline int set_hton_time(uint8_t *dest, time_t sec, time_t nsec)
{
	/* millisec; osd2r00 sec 7.1.2.8 clock description */
	uint64_t time = sec*1000 + nsec/1000000;

	/* only have 48 bits. bits in 48-63 will be lost */
	if ((time & (0xFFFFULL << 48)) != 0)
		return OSD_ERROR;

	set_htontime(dest, time);
	return OSD_OK;
}

/*
 * returns:
 * -EINVAL: generic errors, used_outlen not modified
 * OSD_ERROR: generic system errors, used_outlen not modified
 * ==0: OSD_OK (success), used_outlen stores consumed outbuf len
 */
static int get_utsap(struct osd_device *osd, uint64_t pid, uint64_t oid,
		     void *outbuf, uint64_t outlen, uint32_t *used_outlen)
{
	int ret = 0;
	uint8_t *cp = outbuf;
	struct stat asb, dsb;
	char path[MAXNAMELEN];

	assert(osd && outbuf && used_outlen);

	/*
	 * XXX:SD we are treating the case of len < UTSAP_TOTAL_LEN as
	 * overflow error.
	 * overflow is not considered error. osd2r00 sec 5.2.2.2
	 */
	if (outlen < UTSAP_TOTAL_LEN) {
		*used_outlen = 0;
		goto out;
	}

	memset(cp, 0, UTSAP_TOTAL_LEN);
	set_htonl(&cp[0], USER_TMSTMP_PG);
	set_htonl(&cp[4], UTSAP_TOTAL_LEN - 8);

	get_dfile_name(path, osd->root, pid, oid);
	memset(&dsb, 0, sizeof(dsb));
	ret = stat(path, &dsb);
	if (ret != 0)
		return OSD_ERROR;

	/* XXX: not exactly accurate to stat the entire db for the
	 * access/mod time of one object */
	get_dbname(path, osd->root);
	memset(&asb, 0, sizeof(asb));
	ret = stat(path, &asb);
	if (ret != 0)
		return OSD_ERROR;

	ret = set_hton_time(&cp[UTSAP_CTIME_OFF], dsb.st_ctime,
			    dsb.st_ctim.tv_nsec);
	if (ret != OSD_OK)
		return ret;

	ret = set_hton_time(&cp[UTSAP_ATTR_ATIME_OFF], asb.st_atime,
			    asb.st_atim.tv_nsec);
	if (ret != OSD_OK)
		return ret;
	ret = set_hton_time(&cp[UTSAP_ATTR_MTIME_OFF], asb.st_mtime,
			    asb.st_mtim.tv_nsec);
	if (ret != OSD_OK)
		return ret;

	ret = set_hton_time(&cp[UTSAP_DATA_ATIME_OFF], dsb.st_atime,
			    dsb.st_atim.tv_nsec);
	if (ret != OSD_OK)
		return ret;
	ret = set_hton_time(&cp[UTSAP_DATA_MTIME_OFF], dsb.st_mtime,
			    dsb.st_mtim.tv_nsec);
	if (ret != OSD_OK)
		return ret;

	*used_outlen = UTSAP_TOTAL_LEN;

out:
	return OSD_OK;
}

/*
 * returns:
 * -EINVAL: generic error, used_outlen not set
 * OSD_ERROR: used_outlen not set
 * OSD_OK: on success, sets used_outlen
 */
static int get_utsap_aslist(struct osd_device *osd, uint64_t pid, uint64_t oid,
			    uint32_t number, void *outbuf, uint64_t outlen,
			    uint8_t listfmt, uint32_t *used_outlen)
{
	int ret = 0;
	uint16_t len = 0;
	void *val = NULL;
	uint64_t time = 0;
	char name[ATTR_PAGE_ID_LEN] = {'\0'};
	char path[MAXNAMELEN];
	struct stat sb;

	assert(osd && outbuf && used_outlen);

	switch (number) {
	case 0:
		len = ATTR_PAGE_ID_LEN;
		sprintf(name, "INCITS  T10 User Object Timestamps");
		val = name;
		break;
	case UTSAP_CTIME:
	case UTSAP_DATA_MTIME:
	case UTSAP_DATA_ATIME:
		get_dfile_name(path, osd->root, pid, oid);
		memset(&sb, 0, sizeof(sb));
		ret = stat(path, &sb);
		if (ret != 0)
			return OSD_ERROR;
		len = 6;
		val = &time;
		if (number == UTSAP_CTIME)
			set_hton_time(val, sb.st_ctime, sb.st_ctim.tv_nsec);
		else if (number == UTSAP_DATA_ATIME)
			set_hton_time(val, sb.st_atime, sb.st_atim.tv_nsec);
		else
			set_hton_time(val, sb.st_mtime, sb.st_mtim.tv_nsec);
		break;
	case UTSAP_ATTR_ATIME:
	case UTSAP_ATTR_MTIME:
		get_dbname(path, osd->root);
		memset(&sb, 0, sizeof(sb));
		ret = stat(path, &sb);
		if (ret != 0)
			return OSD_ERROR;
		len = 6;
		val = &time;
		if (number == UTSAP_ATTR_ATIME)
			set_hton_time(val, sb.st_atime, sb.st_atim.tv_nsec);
		else
			set_hton_time(val, sb.st_mtime, sb.st_mtim.tv_nsec);
		break;
	default:
		return OSD_ERROR;
	}

	if (listfmt == RTRVD_CREATE_MULTIOBJ_LIST)
		ret = le_multiobj_pack_attr(outbuf, outlen, oid,
					    USER_TMSTMP_PG, number, len, val);
	else
		ret = le_pack_attr(outbuf, outlen, USER_TMSTMP_PG, number,
				   len, val);

	assert(ret == -EINVAL || ret == -EOVERFLOW || ret > 0);
	if (ret == -EOVERFLOW)
		*used_outlen = 0;
	else if (ret > 0)
		*used_outlen = ret;
	else
		return ret;

	return OSD_OK;
}



/*
 * returns:
 * -EINVAL: generic error
 * OSD_ERROR: in case of error, does not set used_outlen
 * OSD_OK: on success, sets used_outlen
 */
static int get_uiap(struct osd_device *osd, uint64_t pid, uint64_t oid,
		    uint32_t page, uint32_t number, void *outbuf,
		    uint64_t outlen, uint8_t listfmt, uint32_t *used_outlen)
{
	int ret = 0;
	void *val = NULL;
	uint16_t len = 0;
	char name[ATTR_PAGE_ID_LEN];
	char path[MAXNAMELEN];
	struct stat sb;
	struct statfs sfs;
	uint8_t ll[8];
	off_t sz = 0;

	switch (number) {
	case 0:
		len = ATTR_PAGE_ID_LEN;
		sprintf(name, "INCITS  T10 User Object Information");
		val = name;
		break;
	case UIAP_PID:
		set_htonll(ll, pid);
		len = UIAP_PID_LEN;
		val = ll;
		break;
	case UIAP_OID:
		set_htonll(ll, pid);
		len = UIAP_OID_LEN;
		val = ll;
		break;
	case UIAP_USED_CAPACITY:
		len = UIAP_USED_CAPACITY_LEN;
		get_dfile_name(path, osd->root, pid, oid);
		if (!oid) {
			ret = statfs(path, &sfs);
			if (ret != 0)
				return OSD_ERROR;

			sz = (sfs.f_blocks - sfs.f_bfree) * BLOCK_SZ;
		} else {
			ret = stat(path, &sb);
			if (ret != 0)
				return OSD_ERROR;

			sz = sb.st_blocks*BLOCK_SZ;
		}
		set_htonll(ll, sz);
		val = ll;
		break;
	case UIAP_LOGICAL_LEN:
		len = UIAP_LOGICAL_LEN_LEN;
		get_dfile_name(path, osd->root, pid, oid);
		ret = stat(path, &sb);
		if (ret != 0)
			return OSD_ERROR;
		set_htonll(ll, sb.st_size);
		val = ll;
		break;
	case PARTITION_CAPACITY_QUOTA:
		len = UIAP_USED_CAPACITY_LEN;
		get_dfile_name(path, osd->root, pid, oid);
		ret = statfs(path, &sfs);
		osd_debug("PARTITION_CAPACITY_QUOTA statfs(%s)=>%d size=0x%llx\n",
			path, ret, llu(sfs.f_blocks));
		if (ret != 0)
			return OSD_ERROR;
		sz = sfs.f_blocks * BLOCK_SZ;
		set_htonll(ll, sz);
		val = ll;
		break;
	case UIAP_USERNAME:
		return attr_get_attr(osd->dbc, pid, oid, USER_INFO_PG,
					UIAP_USERNAME, outlen, outbuf, listfmt,
					used_outlen);
	default:
		return OSD_ERROR;
	}

	if (listfmt == RTRVD_SET_ATTR_LIST)
		ret = le_pack_attr(outbuf, outlen, page, number, len, val);
	else if (listfmt == RTRVD_CREATE_MULTIOBJ_LIST)
		ret = le_multiobj_pack_attr(outbuf, outlen, oid, page, number,
					    len, val);
	else
		return OSD_ERROR;

	assert(ret == -EINVAL || ret == -EOVERFLOW || ret > 0);
	if (ret == -EOVERFLOW)
		*used_outlen = 0;
	else if (ret > 0)
		*used_outlen = ret;
	else
		return ret;

	return OSD_OK;
}

/*
 * returns:
 * OSD_ERROR: in case of error
 * OSD_OK: on success
 */
static int set_uiap(struct osd_device *osd, uint64_t pid, uint64_t oid,
		    uint32_t number, const void *val, uint16_t len)
{
	int ret = 0;

	switch (number) {
	case UIAP_USERNAME:
		return attr_set_attr(osd->dbc, pid, oid, USER_INFO_PG,
					UIAP_USERNAME, val, len);
	case UIAP_LOGICAL_LEN: {
		char path[MAXNAMELEN];
		uint64_t len = get_ntohll((const uint8_t *)val);
		get_dfile_name(path, osd->root, pid, oid);
		osd_debug("%s: %s %llu\n", __func__, path, llu(len));
		ret = truncate(path, len);
		if (ret < 0)
			return OSD_ERROR;
		else
			return OSD_OK;
	}
	default:
		return OSD_ERROR;
	}
}

static struct init_attr root_info[] = {
	{ROOT_PG + 0, 0, "INCITS  T10 Root Directory             "},
	{ROOT_PG + 0, ROOT_PG + 1, "INCITS  T10 Root Information           "},
	{ROOT_PG + 0, ROOT_PG + 2, "INCITS  T10 Root Quotas                "},
	{ROOT_PG + 2, 0, "INCITS  T10 Root Quotas                "},
	{ROOT_PG + 0, ROOT_PG + 3, "INCITS  T10 Root Timestamps            "},
	{ROOT_PG + 3, 0, "INCITS  T10 Root Timestamps            "},
	{ROOT_PG + 0, ROOT_PG + 5, "INCITS  T10 Root Policy/Security       "},
	{ROOT_PG + 5, 0, "INCITS  T10 Root Policy/Security       "}
};

static struct init_attr partition_info[] = {
	{PARTITION_PG + 0, 0, "INCITS  T10 Partition Directory"},
	{PARTITION_PG + 0, PARTITION_PG + 1,
	       "INCITS  T10 Partition Information"},
	{PARTITION_PG + 1, 0, "INCITS  T10 Partition Information"},
};

static int get_riap(struct osd_device *osd, uint64_t pid, uint64_t oid,
		    uint32_t page, uint32_t number, void *outbuf,
		    uint64_t outlen, uint8_t listfmt, uint32_t *used_outlen)
{
	int ret = 0;
	const void *val = NULL;
	uint16_t len = 0;
	char name[ATTR_PAGE_ID_LEN];
	char path[MAXNAMELEN];
	struct stat sb;
	uint8_t ll[8];
	off_t sz = 0;

	switch (number) {
	case 0:
		/*{ROOT_PG + 1, 0, "INCITS  T10 Root Information"},*/
		len = ATTR_PAGE_ID_LEN;
		sprintf(name, "INCITS  T10 Root Information");
		val = name;
		break;
	case RIAP_OSD_SYSTEM_ID:
		ret = attr_get_attr(osd->dbc, pid, oid, ROOT_INFO_PG,
					RIAP_OSD_SYSTEM_ID_LEN, outlen, outbuf,
					listfmt, used_outlen);
		if (ret == -ENOENT) {
			len = RIAP_OSD_SYSTEM_ID_LEN;
			val = "\xf1\x81\x00\x0eOSC     OSDEMU\x00\x00";
		} else {
			return ret;
		}
		break;
	case RIAP_VENDOR_IDENTIFICATION:
		len = sizeof("OSC");
		val = "OSC";
		break;
	case RIAP_PRODUCT_IDENTIFICATION:
		len = sizeof("OSDEMU");
		val = "OSDEMU";
		break;
	case RIAP_PRODUCT_MODEL:
		len = sizeof("OSD2r05");
		val = "OSD2r05";
		break;
	case RIAP_PRODUCT_REVISION_LEVEL:
		len = RIAP_PRODUCT_REVISION_LEVEL_LEN;
		set_htonl(ll, 117);
		val = ll;
		break;
	case RIAP_PRODUCT_SERIAL_NUMBER:
		len = sizeof("2");
		val = "2";
		break;
	case RIAP_TOTAL_CAPACITY:
		/*FIXME: return capacity of osd->root device*/
		len = RIAP_TOTAL_CAPACITY_LEN;
		set_htonll(ll, -1);
		val = ll;
		break;
	case RIAP_USED_CAPACITY:
		/*FIXME: return used capacity of osd->root device*/
		len = RIAP_USED_CAPACITY_LEN;
		set_htonll(ll, -1);
		val = ll;
		break;
	case RIAP_NUMBER_OF_PARTITIONS:
		/*FIXME: How to find this information*/
		len = RIAP_NUMBER_OF_PARTITIONS_LEN;
		set_htonll(ll, 17);
		val = ll;
		break;
	case RIAP_CLOCK:
		/*FIXME: gettime + saved offset*/
		len = RIAP_CLOCK_LEN;
		set_htonl(ll, 0);
		val = ll;
		break;
	case RIAP_OSD_NAME:
		return attr_get_attr(osd->dbc, pid, oid, ROOT_INFO_PG,
					RIAP_OSD_NAME, outlen, outbuf, listfmt,
					used_outlen);
	default:
		return OSD_ERROR;
	}

	if (listfmt == RTRVD_SET_ATTR_LIST)
		ret = le_pack_attr(outbuf, outlen, page, number, len, val);
	else if (listfmt == RTRVD_CREATE_MULTIOBJ_LIST)
		ret = le_multiobj_pack_attr(outbuf, outlen, oid, page, number,
					    len, val);
	else
		return OSD_ERROR;

	assert(ret == -EINVAL || ret == -EOVERFLOW || ret > 0);
	if (ret == -EOVERFLOW)
		*used_outlen = 0;
	else if (ret > 0)
		*used_outlen = ret;
	else
		return ret;

	return OSD_OK;
}

static int set_riap(struct osd_device *osd, uint64_t pid, uint64_t oid,
		    uint32_t number, const void *val, uint16_t len)
{
	switch (number) {
	/* read only */
	case RIAP_VENDOR_IDENTIFICATION:
	case RIAP_PRODUCT_IDENTIFICATION:
	case RIAP_PRODUCT_MODEL:
	case RIAP_PRODUCT_REVISION_LEVEL:
	case RIAP_PRODUCT_SERIAL_NUMBER:
	case RIAP_TOTAL_CAPACITY:
	case RIAP_USED_CAPACITY:
	case RIAP_NUMBER_OF_PARTITIONS:
	default:
		return OSD_ERROR;

	/* osd-target extension: We let in a system_ID set on LUN
	 * format command.
	 */
	case RIAP_OSD_SYSTEM_ID:
		if (osd->ccap.cdb_srvc_act == OSD_FORMAT_OSD) {
			char system_id[RIAP_OSD_SYSTEM_ID_LEN];

			if (len > RIAP_OSD_SYSTEM_ID_LEN)
				return -EINVAL;

			if (len < RIAP_OSD_SYSTEM_ID_LEN) {
				memcpy(system_id, val, len);
				memset(system_id + len, 0,
				       RIAP_OSD_SYSTEM_ID_LEN - len);
				val = system_id;
			}
			return attr_set_attr(osd->dbc, 0, 0, ROOT_INFO_PG,
					     RIAP_OSD_SYSTEM_ID, val,
					     RIAP_OSD_SYSTEM_ID_LEN);
		} else
			return OSD_ERROR;

	case RIAP_OSD_NAME: {
		char osdname[64];

		snprintf(osdname, min((uint16_t)64U, len), "%s",
			 (const char *)val);
		osd_info("RIAP_OSD_NAME [%s]\n", osdname);

		return attr_set_attr(osd->dbc, 0, 0, ROOT_INFO_PG,
					RIAP_OSD_NAME, val, len);
	}
	case RIAP_CLOCK:
		/* FIXME: Save an offset from current time.
			 return time + offset */
		return OSD_OK;
	}
}

/*
 * returns:
 * OSD_ERROR: for error
 * OSD_OK: on success
 */
static int set_cap(struct osd_device *osd, uint64_t pid, uint64_t oid,
		   uint32_t number, const void *val, uint16_t len)
{
	int ret = 0;
	int present = 0;
	uint64_t cid = 0;

	if (number < UCAP_COLL_PTR_LB || number > UCAP_COLL_PTR_UB)
		return OSD_ERROR;

	if (len == 0) {
		/*
		 * XXX:SD len == 0 we are deleting the (cid, oid)
		 * combination. This does not follow the sematics of the
		 * spec. In future if this has to be changed then one way
		 * would be to add length column and setting length to zero.
		 * Other queries might have to be modified to reflect this
		 * development
		 */
		ret = coll_get_cid(osd->dbc, pid, oid, number, &cid);
		if (ret != 0)
			return OSD_ERROR;

		ret = coll_delete(osd->dbc, pid, cid, oid);
		if (ret != 0)
			return OSD_ERROR;

		return OSD_OK;
	}

	if (len != 8)
		return OSD_ERROR;

	cid = get_ntohll(val);
	ret = obj_ispresent(osd->dbc, pid, cid, &present);
	if (ret != OSD_OK || !present)
		return OSD_ERROR;

	ret = coll_insert(osd->dbc, pid, cid, oid, number);
	if (ret != 0)
		return OSD_ERROR;

	return OSD_OK;
}

/*
 * Create root object and set attributes for root and partition zero.
 * = 0: success
 * !=0: failure
 */
static int osd_initialize_db(struct osd_device *osd)
{
	int i = 0;
	int ret = 0;
	uint64_t pid = 0;
	char *err = NULL;

	if (!osd)
		return -EINVAL;

	memset(&osd->ccap, 0, sizeof(osd->ccap));
	memset(&osd->ic, 0, sizeof(osd->ic));
	memset(&osd->idl, 0, sizeof(osd->idl));

	/* tables already created by osd_db_open, so insertions can be done */
	ret = obj_insert(osd->dbc, ROOT_PID, ROOT_OID, ROOT);
	if (ret != SQLITE_OK)
		goto out;

	/* set root object attributes */
	for (i=0; i<ARRAY_SIZE(root_info); i++) {
		struct init_attr *ia = &root_info[i];
		ret = attr_set_attr(osd->dbc, ROOT_PID , ROOT_OID, ia->page,
				    ia->number, ia->s, strlen(ia->s)+1);
		if (ret != SQLITE_OK)
			goto out;
	}

	/*
	 * partition zero (0,0) has the same object identifier as root object
	 * (0,0).  The attributes will not be overwritten since attr pages
	 * are different.
	 */
	for (i=0; i<ARRAY_SIZE(partition_info); i++) {
		struct init_attr *ia = &partition_info[i];
		ret = attr_set_attr(osd->dbc, ROOT_PID, ROOT_OID, ia->page,
				    ia->number, ia->s, strlen(ia->s)+1);
		if (ret)
			goto out;
	}

	/* assign pid as attr, osd2r00 Section 7.1.2.9 table 92  */
	ret = attr_set_attr(osd->dbc, ROOT_PID, ROOT_OID, PARTITION_PG + 1, 1,
			    &pid, sizeof(pid));

out:
	return ret;
}

struct osd_device *osd_device_alloc(void)
{
	struct osd_device *osd;

	osd = malloc(sizeof(*osd));
	return osd;
}

void osd_device_free(struct osd_device *osd)
{
	free(osd);
}

int osd_open(const char *root, struct osd_device *osd)
{
	int i = 0;
	int ret = 0;
	char path[MAXNAMELEN];
	char *argv[] = { strdup("osd-target"), NULL };

	osd_set_progname(1, argv);  /* for debug messages from libosdutil */
	mhz = get_mhz(); /* XXX: find a better way of profiling */

	if (strlen(root) > MAXROOTLEN) {
		osd_error("strlen(%s) > MAXROOTLEN", root);
		ret = -ENAMETOOLONG;
		goto out;
	}

	memset(osd, 0, sizeof(*osd));

	/* test if root exists and is a directory */
	ret = create_dir(root);
	if (ret != 0) {
		osd_error("!create_dir_root(%s)", root);
		goto out;
	}

	/* test create 'data/dfiles' sub-directory */
	sprintf(path, "%s/%s/", root, dfiles);
	ret = create_dir(path);
	if (ret != 0) {
		osd_error("!create_dir_data/dfiles(%s)", path);
		goto out;
	}

#ifndef __PANASAS_OSD__
	/* to prevent fan-out create 256 subdirs under dfiles */
	for (i = 0; i < 256; i++) {
		sprintf(path, "%s/%s/%02x/", root, dfiles, i);
		ret = create_dir(path);
		if (ret != 0) {
			osd_error("!create_dir_256(%s)", path);
			goto out;
		}
	}
#endif

	/* create 'stranded-files' sub-directory */
	sprintf(path, "%s/%s/", root, stranded);
	ret = create_dir(path);
	if (ret != 0) {
		osd_error("!create_dir_stranded-files(%s)", path);
		goto out;
	}

	/* create 'md' sub-directory */
	sprintf(path, "%s/%s/", root, md);
	ret = create_dir(path);
	if (ret != 0) {
		osd_error("!create_dir_md(%s)", path);
		goto out;
	}

	osd->root = strdup(root);
	if (!osd->root) {
		osd_error("!strdup_root(%s)", root);
		ret = -ENOMEM;
		goto out;
	}
	get_dbname(path, root);

	/* auto-creates db if necessary, and sets osd->dbc */
	ret = osd_db_open(path, osd);
	if (ret != 0 && ret != 1) {
		osd_error("!osd_db_open(%s)", path);
		goto out;
	}
	if (ret == 1) {
		ret = osd_initialize_db(osd);
		if (ret != 0) {
			osd_error("!osd_initialize_db");
			goto out;
		}
	}
	ret = db_exec_pragma(osd->dbc);
out:
	if (ret != 0)
		osd_error("!db_exec_pragma => %d", ret);

	return ret;
}

int osd_set_name(struct osd_device *osd, char *osdname)
{
        int ret = 0;

        osd_info("Setting osdname => %s",osdname);

        ret = attr_set_attr(osd->dbc, 0, 0, ROOT_INFO_PG,RIAP_OSD_NAME,
                            osdname, strlen(osdname));

        if ( OSD_OK != ret){
                osd_error("!attr_set_attr => %d", ret);
        }

        return ret;
}

int osd_close(struct osd_device *osd)
{
	int ret;

	ret = osd_db_close(osd);
	if (ret != 0)
		osd_error("%s: osd_db_close", __func__);
	free(osd->root);
	osd->root = NULL;
	return ret;
}

int osd_begin_txn(struct osd_device *osd)
{
	return db_begin_txn(osd->dbc);
}

int osd_end_txn(struct osd_device *osd)
{
	return db_end_txn(osd->dbc);
}

/*
 * Externally callable error response generators.
 */
int osd_error_unimplemented(uint16_t action, uint8_t *sense)
{
	int ret;

	osd_debug(__func__);
	ret = sense_basic_build(sense, OSD_SSK_ILLEGAL_REQUEST, 0, 0, 0);
	return ret;
}

int osd_error_bad_cdb(uint8_t *sense)
{
	int ret;

	osd_debug(__func__);
	/* should probably add what part of the cdb was bad */
	ret = sense_basic_build(sense, OSD_SSK_ILLEGAL_REQUEST, 0, 0, 0);
	return ret;
}

/*
 * Commands
 */

/*
 * Sec 6.2 in osd2r01.pdf
 */
static int contig_append(struct osd_device *osd, uint64_t pid, uint64_t oid,
	       uint64_t len, const uint8_t *appenddata, uint8_t *sense)
{
	int fd;
	int ret;
	off64_t off;
	char path[MAXNAMELEN];

	osd_debug("%s: pid %llu oid %llu len %llu data %p", __func__,
		  llu(pid), llu(oid), llu(len), appenddata);

	if (!osd || !osd->root || !osd->dbc || !appenddata || !sense)
		goto out_cdb_err;

	if (!(pid >= USEROBJECT_PID_LB && oid >= USEROBJECT_OID_LB))
		goto out_cdb_err;

	get_dfile_name(path, osd->root, pid, oid);
	fd = open(path, O_WRONLY|O_LARGEFILE); /* fails on non-existent obj */
	if (fd < 0)
		goto out_cdb_err;

	/* seek to the end of logical length: current size of the object */
	off = lseek(fd, 0, SEEK_END);
	if (off < 0)
		goto out_hw_err;

	ret = pwrite(fd, appenddata, len, off);
	if (ret < 0 || (uint64_t) ret != len)
		goto out_hw_err;

	ret = close(fd);
	if (ret != 0)
		goto out_hw_err;

	fill_ccap(&osd->ccap, NULL, USEROBJECT, pid, oid, off);
	return OSD_OK; /* success */

out_hw_err:
	ret = sense_build_sdd(sense, OSD_SSK_HARDWARE_ERROR,
			      OSD_ASC_INVALID_FIELD_IN_CDB, pid, oid);
	return ret;

out_cdb_err:
	ret = sense_build_sdd(sense, OSD_SSK_ILLEGAL_REQUEST,
			      OSD_ASC_INVALID_FIELD_IN_CDB, pid, oid);
	return ret;
}

static int sgl_append(struct osd_device *osd, uint64_t pid, uint64_t oid,
	       uint64_t len, const uint8_t *appenddata, uint8_t *sense)
{
	int fd;
	int ret;
	off64_t off;
	char path[MAXNAMELEN];
	uint64_t pairs, data_offset, offset_val, hdr_offset, length;
	unsigned int i;

	osd_debug("%s: pid %llu oid %llu len %llu data %p", __func__,
		  llu(pid), llu(oid), llu(len), appenddata);

	if (!osd || !osd->root || !osd->dbc || !appenddata || !sense)
		goto out_cdb_err;

	pairs = get_ntohll(appenddata);
	data_offset = (pairs * sizeof(uint64_t) * 2) + sizeof(uint64_t);
	assert(pairs != 0);

	osd_debug("%s: offset,len pairs %llu", __func__, llu(pairs));
	osd_debug("%s: data offset %llu", __func__, llu(data_offset));


	if (!(pid >= USEROBJECT_PID_LB && oid >= USEROBJECT_OID_LB))
		goto out_cdb_err;

	get_dfile_name(path, osd->root, pid, oid);
	fd = open(path, O_WRONLY|O_LARGEFILE); /* fails on non-existent obj */
	if (fd < 0)
		goto out_cdb_err;

	/* seek to the end of logical length: current size of the object */
	off = lseek(fd, 0, SEEK_END);
	if (off < 0)
		goto out_hw_err;

	hdr_offset = sizeof(uint64_t); /* skip count of offset/len pairs */

	for (i=0; i<pairs; i++) {
		offset_val = get_ntohll(appenddata + hdr_offset); /* offset into dest */
		hdr_offset += sizeof(uint64_t);

		length = get_ntohll(appenddata + hdr_offset);   /* length */
		hdr_offset += sizeof(uint64_t);

		osd_debug("%s: Offset: %llu Length: %llu", __func__, llu(offset_val+off),
			llu(length));

		osd_debug("%s: Position in data buffer: %llu", __func__, llu(data_offset));

		osd_debug("%s: ------------------------------", __func__);
		ret = pwrite(fd, appenddata+data_offset, length, offset_val+off);
		data_offset += length;
		osd_debug("%s: return value is %d", __func__, ret);
		if (ret < 0 || (uint64_t)ret != length)
			goto out_hw_err;
	}

	ret = close(fd);
	if (ret != 0)
		goto out_hw_err;

	fill_ccap(&osd->ccap, NULL, USEROBJECT, pid, oid, off);
	return OSD_OK; /* success */

out_hw_err:
	ret = sense_build_sdd(sense, OSD_SSK_HARDWARE_ERROR,
			      OSD_ASC_INVALID_FIELD_IN_CDB, pid, oid);
	return ret;

out_cdb_err:
	ret = sense_build_sdd(sense, OSD_SSK_ILLEGAL_REQUEST,
			      OSD_ASC_INVALID_FIELD_IN_CDB, pid, oid);
	return ret;
}

static int vec_append(struct osd_device *osd, uint64_t pid, uint64_t oid,
	       uint64_t len, const uint8_t *appenddata, uint8_t *sense)
{
	int fd;
	int ret;
	off64_t off;
	char path[MAXNAMELEN];
	uint64_t stride, data_offset, offset_val, hdr_offset, length, bytes;
	unsigned int i;

	osd_debug("%s: pid %llu oid %llu len %llu data %p", __func__,
		  llu(pid), llu(oid), llu(len), appenddata);

	if (!osd || !osd->root || !osd->dbc || !appenddata || !sense)
		goto out_cdb_err;

	stride = get_ntohll(appenddata);
	hdr_offset = sizeof(uint64_t);
	length = get_ntohll(appenddata + hdr_offset);

	osd_debug("%s: stride is %llu and len is %llu", __func__, llu(stride), llu(length));

	if (!(pid >= USEROBJECT_PID_LB && oid >= USEROBJECT_OID_LB))
		goto out_cdb_err;

	get_dfile_name(path, osd->root, pid, oid);
	fd = open(path, O_WRONLY|O_LARGEFILE); /* fails on non-existent obj */
	if (fd < 0)
		goto out_cdb_err;

	/* seek to the end of logical length: current size of the object */
	off = lseek(fd, 0, SEEK_END);
	if (off < 0)
		goto out_hw_err;

	data_offset = hdr_offset + sizeof(uint64_t);

	bytes = len - (2*sizeof(uint64_t));

	osd_debug("%s: bytes to write is %llu", __func__, llu(bytes));
	offset_val = 0;
	while (bytes > 0) {
		osd_debug("%s: Position in data buffer: %llu", __func__,
			   llu(data_offset));
		osd_debug("%s: Offset: %llu", __func__, llu(offset_val + off));
		osd_debug("%s: ------------------------------", __func__);
		ret = pwrite(fd, appenddata+data_offset, length, offset_val+off);
		if (ret < 0 || (uint64_t)ret != length)
			goto out_hw_err;
		data_offset += length;
		offset_val += stride;
		bytes -= length;
		if (bytes < length)
			length = bytes;
		osd_debug("%s: Total Bytes Left to write: %llu", __func__,
			  llu(bytes));
	}

	ret = close(fd);
	if (ret != 0)
		goto out_hw_err;

	fill_ccap(&osd->ccap, NULL, USEROBJECT, pid, oid, off);
	return OSD_OK; /* success */

out_hw_err:
	ret = sense_build_sdd(sense, OSD_SSK_HARDWARE_ERROR,
			      OSD_ASC_INVALID_FIELD_IN_CDB, pid, oid);
	return ret;

out_cdb_err:
	ret = sense_build_sdd(sense, OSD_SSK_ILLEGAL_REQUEST,
			      OSD_ASC_INVALID_FIELD_IN_CDB, pid, oid);
	return ret;
}

int osd_append(struct osd_device *osd, uint64_t pid, uint64_t oid,
	       uint64_t len, const uint8_t *appenddata, uint32_t cdb_cont_len, 
	       uint8_t *sense, uint8_t ddt)
{
	/*figure out what kind of write it is based on ddt and call appropriate
	write function*/

	switch(ddt) {
		case DDT_CONTIG: {
			return contig_append(osd, pid, oid, len,
					     appenddata+cdb_cont_len, sense);
		}
		case DDT_SGL: {
			return sgl_append(osd, pid, oid, len,
					  appenddata+cdb_cont_len, sense);
		}
		case DDT_VEC: {
			return vec_append(osd, pid, oid, len, appenddata, 
					  sense);
		}
		default: {
			return sense_build_sdd(sense, OSD_SSK_ILLEGAL_REQUEST,
			               OSD_ASC_INVALID_FIELD_IN_CDB, pid, oid);
		}
	}
}


static int osd_create_datafile(struct osd_device *osd, uint64_t pid,
			       uint64_t oid)
{
	int ret = 0;
	char path[MAXNAMELEN];
	struct stat sb;

	get_dfile_name(path, osd->root, pid, oid);
	ret = stat(path, &sb);
	if (ret == 0 && S_ISREG(sb.st_mode)) {
		return -EEXIST;
	} else if (ret == -1 && errno == ENOENT) {
#ifdef __PANASAS_OSDSIM__
		char *smoog;
		smoog = strrchr(path, '/');
		*smoog = '\0';
		create_dir(path);
		osd_error("%s: panasas create %s directory %m", __func__,path);
		*smoog = '/';
#endif
		ret = creat(path, 0666);
		if (ret <= 0)
			return ret;
		close(ret);
	} else {
		return ret;
	}

	return 0;
}

static inline void osd_remove_tmp_objects(struct osd_device *osd, uint64_t pid,
					  uint64_t start_oid, uint64_t end_oid,
					  uint8_t *sense, uint32_t cdb_cont_len)
{
	uint64_t j = 0;
	for (j = start_oid; j < end_oid; j++)
	        osd_remove(osd, pid, j, cdb_cont_len, sense); /* ignore ret */
}

static int osd_init_attr(struct osd_device *osd, uint64_t pid, uint64_t oid)
{
	int ret = 0;
	uint64_t val = 0;

	ret = attr_set_attr(osd->dbc, pid, oid, USER_TMSTMP_PG, 0,
			    incits.user_tmstmp_page,
			    sizeof(incits.user_tmstmp_page));
	if (ret != 0)
		return ret;

	ret = attr_set_attr(osd->dbc, pid, oid, USER_ATOMICS_PG, 0,
			    incits.user_atomic_page,
			    sizeof(incits.user_atomic_page));
	if (ret != 0)
		return ret;

	val = 0;
	ret = attr_set_attr(osd->dbc, pid, oid, USER_ATOMICS_PG, UAP_CAS,
			    &val, sizeof(val));
	if (ret != 0)
		return ret;
	ret = attr_set_attr(osd->dbc, pid, oid, USER_ATOMICS_PG, UAP_FA, &val,
			    sizeof(val));
	if (ret != 0)
		return ret;

	return OSD_OK;
}

int osd_clear(struct osd_device *osd, uint64_t pid, uint64_t oid,
              uint64_t len, uint64_t offset, uint32_t cdb_cont_len,
	      uint8_t *sense)
{
	int ret;
	int fd=-1;
	char path[MAXNAMELEN];
	char *dinbuf;
	dinbuf = calloc(len, sizeof(char));

	if (dinbuf == NULL)
	        goto out_hw_err;
	
	osd_debug("%s: pid %llu oid %llu len %llu offset %llu",
		  __func__, llu(pid), llu(oid), llu(len), llu(offset));
	
	assert(osd && osd->root && osd->dbc && sense);

	if (!(pid >= USEROBJECT_PID_LB && oid >= USEROBJECT_OID_LB))
	        goto out_cdb_err;

	get_dfile_name(path, osd->root, pid, oid);
	
	fd = open(path, O_RDWR|O_LARGEFILE); /* fails on non-existent obj */
	
	if (fd < 0)
		goto out_cdb_err;

	ret = pwrite(fd, dinbuf, len, offset); /* writing null characters to file */

	if (ret < 0 || (uint64_t)ret != len)
		goto out_hw_err;
	
	ret = close(fd);
	if (ret != 0)
		goto out_hw_err;
	        
	fill_ccap(&osd->ccap, NULL, USEROBJECT, pid, oid, 0);

	free(dinbuf);

	return OSD_OK; /* success */

out_hw_err:
	ret = sense_build_sdd(sense, OSD_SSK_HARDWARE_ERROR,
		     OSD_ASC_INVALID_FIELD_IN_CDB, pid, oid);
	if(fd >= 0) 
	        close(fd);
	if(dinbuf != NULL)
	        free(dinbuf);
	return ret;

out_cdb_err:
	ret = sense_build_sdd(sense, OSD_SSK_ILLEGAL_REQUEST,
		     OSD_ASC_INVALID_FIELD_IN_CDB, pid, oid);
	free(dinbuf);
	return ret;
}


/*
 * XXX: get/set attributes to be handled in cdb.c
 */

int osd_copy_user_objects(struct osd_device *osd, uint64_t pid, uint64_t requested_oid, 
			  const struct copy_user_object_source *cuos,
			  uint8_t dupl_method, uint8_t *sense)
{
        int ret = 0;
	uint64_t oid = 0;
	int present = 0;
	uint64_t source_pid;
	uint64_t source_oid;

	osd_debug("%s: pid: %llu cid %llu", __func__, llu(pid),
		  llu(requested_oid));
	
	if (pid == 0 || pid < COLLECTION_PID_LB)
	          goto out_cdb_err;

	if (requested_oid != 0 && requested_oid < USEROBJECT_OID_LB)
	          goto out_cdb_err;
	
	source_pid = get_ntohll(&cuos->source_pid);
	source_oid = get_ntohll(&cuos->source_oid);

	/* verify that source_pid & source_oid exist */
	ret = obj_ispresent(osd->dbc, source_pid, PARTITION_OID, &present);
	if (ret != OSD_OK || !present)
		goto out_cdb_err;

	ret = obj_ispresent(osd->dbc, source_pid, source_oid, &present);
	if (ret != OSD_OK || !present)
		goto out_cdb_err;

	/* verify that destination_pid exists */
	ret = obj_ispresent(osd->dbc, pid, PARTITION_OID, &present);
	if (ret != OSD_OK || !present)
	        goto out_cdb_err;

	if (requested_oid == 0) {
	        if (osd->ic.cur_pid == pid) { /* cache hit */
		        oid = osd->ic.next_id;
			osd->ic.next_id++;
		} else {
			ret = obj_get_nextoid(osd->dbc, pid, &oid);
			if (ret != 0)
				goto out_hw_err;
			osd->ic.cur_pid = pid;
			osd->ic.next_id = oid + 1;
		}
		if (oid == 1) {
			oid = USEROBJECT_OID_LB; /* first oid in partition */
			osd->ic.next_id = oid + 1;
		}  
	} else {
	        ret = obj_ispresent(osd->dbc, pid, requested_oid, &present);
		if (ret != OSD_OK || present)
			goto out_cdb_err; /* requested_oid exists! */
		oid = requested_oid; /* requested_oid works! */

		/*XXX: invalidate cache */
		osd->ic.cur_pid = osd->ic.next_id = 0;
	}
 
	if (dupl_method == DEFAULT) {
	        /* call function to copy userobject data from source object in source partition 
		   to destination object in destination partition */
	}
	if (cuos->cpy_attr == 1) {
	        /* call function to copy attribute data from source object in source partition 
		   to destination object in destination partition */
	}

	fill_ccap(&osd->ccap, NULL, USEROBJECT, pid, oid, 0);	
	return OSD_OK; /* success */


out_cdb_err:
	return sense_build_sdd(sense, OSD_SSK_ILLEGAL_REQUEST,
			       OSD_ASC_INVALID_FIELD_IN_CDB,
			       pid, requested_oid);
out_hw_err:
	osd->ic.cur_pid = osd->ic.next_id = 0; /* invalidate cache */
	return sense_build_sdd(sense, OSD_SSK_HARDWARE_ERROR,
			       OSD_ASC_INVALID_FIELD_IN_CDB,
			       pid, requested_oid);
  
        return ret;
}

int osd_create(struct osd_device *osd, uint64_t pid, uint64_t requested_oid,
	       uint16_t numoid, uint32_t cdb_cont_len, uint8_t *sense)
{
	int ret = 0;
	int present = 0;
	uint64_t i = 0;
	uint64_t oid = 0;

	TICK_TRACE(osd_create);
	osd_debug("%s: pid %llu requested oid %llu numoid %hu", __func__,
		  llu(pid), llu(requested_oid), numoid);

	assert(osd && osd->root && osd->dbc && sense);

	if (pid == 0 || pid < USEROBJECT_PID_LB)
		goto out_illegal_req;
                
	if (requested_oid != 0 && requested_oid < USEROBJECT_OID_LB)
		goto out_illegal_req;

	/* Make sure partition is present. */
	ret = obj_ispresent(osd->dbc, pid, PARTITION_OID, &present);
	if (ret != OSD_OK || !present)
		goto out_illegal_req;

	if (numoid > 1 && requested_oid != 0)
		goto out_illegal_req;

	if (requested_oid == 0) {
		/*
		 * XXX: there should be a better way of getting next maximum
		 * oid using SQL itself
		 */
		if (osd->ic.cur_pid == pid) { /* cache hit */
			oid = osd->ic.next_id;
			osd->ic.next_id++;
		} else {
			ret = obj_get_nextoid(osd->dbc, pid, &oid);
			if (ret != 0)
				goto out_hw_err;
			osd->ic.cur_pid = pid;
			osd->ic.next_id = oid + 1;
		}
		if (oid == 1) {
			oid = USEROBJECT_OID_LB; /* first oid in partition */
			osd->ic.next_id = oid + 1;
		}
	} else {
		ret = obj_ispresent(osd->dbc, pid, requested_oid, &present);
		if (ret != OSD_OK || present)
			goto out_illegal_req; /* requested_oid exists! */
		oid = requested_oid; /* requested_oid works! */

		/*XXX: invalidate cache */
		osd->ic.cur_pid = osd->ic.next_id = 0;
	}

	if (numoid == 0)
		numoid = 1; /* create atleast one object */

	for (i = oid; i < (oid + numoid); i++) {
		ret = obj_insert(osd->dbc, pid, i, USEROBJECT);
		if (ret != 0) {
		        osd_remove_tmp_objects(osd, pid, oid, i, sense, cdb_cont_len);
			goto out_hw_err;
		}

		TICK_TRACE(osd_create_datafile);
		ret = osd_create_datafile(osd, pid, i);
		if (ret != 0) {
			obj_delete(osd->dbc, pid, i);
			osd_remove_tmp_objects(osd, pid, oid, i, sense, cdb_cont_len);
			goto out_hw_err;
		}

#if 0
		ret = osd_init_attr(osd, pid, i);
		if (ret != 0) {
			char path[MAXNAMELEN];
			get_dfile_name(path, osd->root, pid, i);
			unlink(path);
			obj_delete(osd->dbc, pid, i);
			osd_remove_tmp_objects(osd, pid, oid, i, sense, cdb_cont_len);
			goto out_hw_err;
		}
#endif
	}
	osd->ic.next_id += (numoid - 1);

	/* fill CCAP with highest oid, osd2r00 Sec 6.3, 3rd last para */
	fill_ccap(&osd->ccap, NULL, USEROBJECT, pid, (oid+numoid-1), 0);
	TICK_TRACE(osd_create);
	return OSD_OK; /* success */

out_illegal_req:
	return sense_build_sdd(sense, OSD_SSK_ILLEGAL_REQUEST,
			       OSD_ASC_INVALID_FIELD_IN_CDB,
			       pid, requested_oid);

out_hw_err:
	osd->ic.cur_pid = osd->ic.next_id = 0; /* invalidate cache */
	return sense_build_sdd(sense, OSD_SSK_HARDWARE_ERROR,
			       OSD_ASC_INVALID_FIELD_IN_CDB,
			       pid, requested_oid);
}





int osd_create_and_write(struct osd_device *osd, uint64_t pid,
			 uint64_t oid, uint64_t len, uint64_t offset,
			 const uint8_t *data, uint32_t cdb_cont_len,
			 const struct sg_list *sglist,
			 uint8_t *sense, uint8_t ddt)
{
	int ret;

	ret = osd_create(osd, pid, oid, 1, cdb_cont_len, sense);
	if (ret) {
		return ret;
	}

	ret = osd_write(osd, pid, oid, len, offset, data, sglist, sense, ddt);
	if (ret) {
	        osd_remove(osd, pid, oid, cdb_cont_len, sense);
		return ret;
	}

	return ret;

}

/* osd2r01 sec. 6.5 */
int osd_create_collection(struct osd_device *osd, uint64_t pid,
			  uint64_t requested_cid, uint32_t cdb_cont_len, uint8_t *sense)
{
	int ret = 0;
	uint64_t cid = 0;
	int present = 0;

	osd_debug("%s: pid: %llu cid %llu", __func__, llu(pid),
		  llu(requested_cid));

	assert(osd && osd->root && osd->dbc && sense);

	if (pid == 0 || pid < COLLECTION_PID_LB)
		goto out_cdb_err;

	if (requested_cid != 0 && requested_cid < COLLECTION_OID_LB)
		goto out_cdb_err;

	/* Make sure partition is present */
	ret = obj_ispresent(osd->dbc, pid, PARTITION_OID, &present);
	if (ret != OSD_OK || !present)
		goto out_cdb_err;

	/*
	 * Collections and Userobjects share same namespace, so we can use
	 * get_nextoid.
	 */
	if (requested_cid == 0) {
		/*
		 * XXX: there should be a better way of getting next maximum
		 * oid using SQL itself
		 */
		if (osd->ic.cur_pid == pid) { /* cache hit */
			cid = osd->ic.next_id;
			osd->ic.next_id++;
		} else {
			ret = obj_get_nextoid(osd->dbc, pid, &cid);
			if (ret != 0)
				goto out_hw_err;
			osd->ic.cur_pid = pid;
			osd->ic.next_id = cid + 1;
		}
		if (cid == 1) {
			cid = COLLECTION_OID_LB; /* first id in partition */
			osd->ic.next_id = cid + 1;
		}
	} else {
		/* Make sure requested_cid doesn't already exist */
		ret = obj_ispresent(osd->dbc, pid, requested_cid, &present);
		if (ret != OSD_OK || present)
			goto out_cdb_err;
		cid = requested_cid;

		/*XXX: invalidate cache */
		osd->ic.cur_pid = osd->ic.next_id = 0;
	}

	/* if cid already exists, obj_insert will fail */
	ret = obj_insert(osd->dbc, pid, cid, COLLECTION);
	if (ret)
		goto out_cdb_err;

	/* TODO: set some of the default attributes of the collection */

	fill_ccap(&osd->ccap, NULL, COLLECTION, pid, cid, 0);
	return OSD_OK; /* success */

out_cdb_err:
	return sense_build_sdd(sense, OSD_SSK_ILLEGAL_REQUEST,
			       OSD_ASC_INVALID_FIELD_IN_CDB,
			       requested_cid, 0);
out_hw_err:
	osd->ic.cur_pid = osd->ic.next_id = 0; /* invalidate cache */
	return sense_build_sdd(sense, OSD_SSK_HARDWARE_ERROR,
			       OSD_ASC_INVALID_FIELD_IN_CDB,
			       requested_cid, 0);
}

int osd_create_partition(struct osd_device *osd, uint64_t requested_pid,
			 uint32_t cdb_cont_len, uint8_t *sense)
{
	int ret = 0;
	uint64_t pid = 0;

	osd_debug("%s: pid %llu", __func__, llu(requested_pid));

	assert(osd && osd->root && osd->dbc && sense);

	if (requested_pid != 0 && requested_pid < PARTITION_PID_LB)
		goto out_cdb_err;

	if (requested_pid == 0) {
		ret = obj_get_nextpid(osd->dbc, &pid);
		if (ret != 0)
			goto out_hw_err;
		if (pid == 1)
			pid = PARTITION_PID_LB; /* firstever pid */
	} else {
		pid = requested_pid;
	}

#ifdef __PANASAS_OSD__
	char path[MAXNAMELEN];
	get_dfile_name(path, osd->root, pid, 0);
	create_dir(path);
	osd_error("%s: panasas create %s directory %m", __func__,path);
#endif
	/* if pid already exists, obj_insert will fail */
	ret = obj_insert(osd->dbc, pid, PARTITION_OID, PARTITION);
	if (ret)
		goto out_cdb_err;

	fill_ccap(&osd->ccap, NULL, PARTITION, pid, PARTITION_OID, 0);
	return OSD_OK; /* success */

out_cdb_err:
	return sense_build_sdd(sense, OSD_SSK_ILLEGAL_REQUEST,
			       OSD_ASC_INVALID_FIELD_IN_CDB,
			       requested_pid, 0);

out_hw_err:
	return sense_build_sdd(sense, OSD_SSK_HARDWARE_ERROR,
			       OSD_ASC_INVALID_FIELD_IN_CDB,
			       requested_pid, 0);
}

static inline int is_linked_coll(uint64_t source_cid)
{
        return source_cid > LINKED_COLLECTION_OID_LB;
}

static inline int is_tracking_coll(uint64_t source_cid)
{
        return source_cid = 0x1082;
}

static inline int is_spontaneous_coll(uint64_t source_cid)
{
        return source_cid < TRACKING_COLLECTION_OID_LB;
}

int osd_create_user_tracking_collection(struct osd_device *osd, uint64_t pid, 
					uint64_t requested_cid,	uint64_t source_cid,
				        uint32_t cdb_cont_len, uint8_t *sense)
{
	int ret = 0;
	uint64_t cid = 0;
	int present = 0;
	int to_do_1=0, to_do_2=0, to_do_3=0;

	osd_debug("%s: pid %llu requested_cid %llu source_cid %llu cdb_cont_len %u",
		  __func__, llu(pid), llu(requested_cid), llu(source_cid), cdb_cont_len);

	assert(osd && osd->root && osd->dbc && sense);

	if (pid == 0 || pid < COLLECTION_PID_LB)
		goto out_cdb_err;

	if (requested_cid != 0 && requested_cid < COLLECTION_OID_LB)
		goto out_cdb_err;

	/* Make sure partition is present */
	ret = obj_ispresent(osd->dbc, pid, PARTITION_OID, &present);
	if (ret != OSD_OK || !present)
		goto out_cdb_err;

	/* Make sure source collection is present */
	ret = obj_ispresent(osd->dbc, pid, source_cid, &present);
	if (ret != OSD_OK || !present)
		goto out_cdb_err;

	/* Checks on cdb_cont_len */
	if (((source_cid != 0) && (cdb_cont_len == 0)) || ((source_cid == 0) && (cdb_cont_len != 0)))
	        goto out_cdb_err;
		
	/* to_do_1 = CDB continuation segment does not contain one extension capabilities CDB continuation descriptor(5.4.6)
	   to_do_2 = CDB continuation segment contains any cdb continuation descriptor other than the extension capabilities CDB 
	   continuation descriptor */
	if ((cdb_cont_len !=0) && (to_do_1 || to_do_2))
	        goto out_cdb_err;

	/* Checking validity of the source collection */
	if (source_cid != 0) {

	        if ((! is_linked_coll(source_cid)) && (! is_tracking_coll(source_cid)) && (! is_spontaneous_coll(source_cid)))
		        goto out_cdb_err;
	
		/* to_do_3 = the active command status attribute in the Command Tracking attribute page(7.1.3.20) is not set to zero*/
		else if (is_tracking_coll(source_cid) && to_do_3)
		        goto out_cdb_err;  
	}

	if (requested_cid == 0) {
		
		if (osd->ic.cur_pid == pid) { /* cache hit */
		        cid = osd->ic.next_id;
			osd->ic.next_id++;
		}else {
		        ret = obj_get_nextoid(osd->dbc, pid, &cid);
			if (ret != 0)
			        goto out_hw_err;
			osd->ic.cur_pid = pid;
			osd->ic.next_id = cid + 1;
		}
		if (cid == 1) {
		        cid = COLLECTION_OID_LB; /* first id in partition */
			osd->ic.next_id = cid + 1;
		} 
	}		  

	else {
		/* Make sure requested_cid doesn't already exist */
		ret = obj_ispresent(osd->dbc, pid, requested_cid, &present);
		if (ret != OSD_OK || present)
			goto out_cdb_err;
		cid = requested_cid;

		/*XXX: invalidate cache */
		osd->ic.cur_pid = osd->ic.next_id = 0;
	}
	
	if (source_cid != 0) {
	        /* Copy source collection members to destination collection */ 
	        ret = coll_copyoids(osd->dbc, pid, cid, source_cid);
		if (ret != 0)
		        goto out_hw_err;
	}

	/* if cid already exists, obj_insert will fail */
	ret = obj_insert(osd->dbc, pid, cid, COLLECTION);
	if (ret)
		goto out_cdb_err;

	fill_ccap(&osd->ccap, NULL, COLLECTION, pid, cid, 0);
	return OSD_OK; /* success */

out_cdb_err:
	return sense_build_sdd(sense, OSD_SSK_ILLEGAL_REQUEST,
			       OSD_ASC_INVALID_FIELD_IN_CDB,
			       requested_cid, 0);
out_hw_err:
	osd->ic.cur_pid = osd->ic.next_id = 0; 
	return sense_build_sdd(sense, OSD_SSK_HARDWARE_ERROR,
			       OSD_ASC_INVALID_FIELD_IN_CDB,
			       requested_cid, 0);
}

int osd_flush(struct osd_device *osd, uint64_t pid, uint64_t oid,
	      uint64_t len, uint64_t offset, int flush_scope, uint32_t cdb_cont_len,
	      uint8_t *sense)
{
	char path[MAXNAMELEN];
	int ret, fd=-1;
	struct stat sb;
	
	osd_debug("%s: pid %llu oid %llu scope %d", __func__, llu(pid),
		  llu(oid), flush_scope);

	assert(osd && osd->root && osd->dbc && sense);

	if (!(pid >= USEROBJECT_PID_LB && oid >= USEROBJECT_OID_LB))
		goto out_cdb_err;

	get_dfile_name(path, osd->root, pid, oid);
	fd = open(path, O_RDWR|O_LARGEFILE); /* fails on non-existent obj */
	if (fd < 0)
		goto out_cdb_err;

	if (flush_scope == 0) {   /* flush data and attributes */
		ret = fdatasync(fd);
		if (ret)
			goto out_hw_err;
		/* flush attribute to be implemented */
	}

	else if (flush_scope == 2) {  /* flush user object data range & attributes */
                
	        ret = stat(path, &sb);
		if(ret) {
		        close(fd);
		        return OSD_ERROR;
		}
	
	        /* Offset beyond user object length */
	        if(offset > (uint64_t)sb.st_size)
	                goto out_cdb_err; 
	  
	        /* Designated bytes beyond object length, only flush bytes within length */
		else if(len > ((uint64_t)sb.st_size - offset)) {
		        ret = sync_file_range(fd, offset, sb.st_size - offset, 0);
			if (ret)
			        goto out_hw_err;
			/* flush attribute to be implemented */
		        ret = close(fd);
		        return OSD_OK;  /* success */
		}
		
		/* Normal Flush */
		ret = sync_file_range(fd, offset, len, 0);  
		if (ret)
		        goto out_hw_err;
		/* flush attribute to be implemented */
	}
	
	else {  /* flush_scope = 1, flush attribute only */
	        /* flush attribute to be implemented */
	  	osd_debug(__func__);
	        return osd_error_unimplemented(0, sense);
	}
		      
	/* attributes always flushed?  need sqlite call here? */

	ret = close(fd);
	if (ret)
		goto out_hw_err;

	fill_ccap(&osd->ccap, NULL, USEROBJECT, pid, oid, 0);
	return OSD_OK; /* success */

out_hw_err:
	ret = sense_build_sdd(sense, OSD_SSK_HARDWARE_ERROR,
			      OSD_ASC_INVALID_FIELD_IN_CDB, pid, oid);
	if(fd >= 0)
	        close(fd);
	return ret;

out_cdb_err:
	ret = sense_build_sdd(sense, OSD_SSK_ILLEGAL_REQUEST,
			      OSD_ASC_INVALID_FIELD_IN_CDB, pid, oid);
	if(fd >= 0)
	        close(fd);
	return ret;
}

int osd_flush_collection(struct osd_device *osd, uint64_t pid, uint64_t cid,
			 int flush_scope, uint32_t cdb_cont_len, uint8_t *sense)
{
	osd_debug(__func__);
	return osd_error_unimplemented(0, sense);
}


int osd_flush_osd(struct osd_device *osd, int flush_scope, uint32_t cdb_cont_len,
		  uint8_t *sense)
{
	osd_debug(__func__);
	return osd_error_unimplemented(0, sense);
}


int osd_flush_partition(struct osd_device *osd, uint64_t pid, int flush_scope,
		        uint32_t cdb_cont_len, uint8_t *sense)
{
	osd_debug(__func__);
	return osd_error_unimplemented(0, sense);
}

/*
 * Destroy the db and start over again.
 */
int osd_format_osd(struct osd_device *osd, uint64_t capacity, uint32_t cdb_cont_len, uint8_t *sense)
{
	int ret;
	char *root = NULL;
	char path[MAXNAMELEN];
	struct stat sb;

	osd_debug("%s: capacity %llu MB", __func__, llu(capacity >> 20));

	assert(osd && osd->root && osd->dbc && sense);

	root = strdup(osd->root);

	get_dbname(path, root);
	if (stat(path, &sb) != 0) {
		osd_error_errno("%s: DB %s does not exist, creating it",
				__func__, path);
		goto create;
	}

	ret = osd_close(osd);
	if (ret) {
		osd_error("%s: DB close failed, ret %d", __func__, ret);
		goto out_sense;
	}

	sprintf(path, "%s/%s/", root, md);
	ret = empty_dir(path);
	if (ret) {
		osd_error("%s: empty_dir %s failed", __func__, path);
		goto out_sense;
	}

	sprintf(path, "%s/%s", root, stranded);
	ret = empty_dir(path);
	if (ret) {
		osd_error("%s: empty_dir %s failed", __func__, path);
		goto out_sense;
	}

#ifndef __PANASAS_OSD__
	sprintf(path, "%s/%s", root, dfiles);
	ret = empty_dir(path);
	if (ret) {
		osd_error("%s: empty_dir %s failed", __func__, path);
		goto out_sense;
	}
#endif

create:
	ret = osd_open(root, osd); /* will create files/dirs under root */
	if (ret != 0) {
		osd_error("%s: osd_open %s failed", __func__, root);
		goto out_sense;
	}
	memset(&osd->ccap, 0, sizeof(osd->ccap)); /* reset ccap */
	ret = OSD_OK;
	goto out;

out_cdb_err:
	ret = sense_build_sdd(sense, OSD_SSK_ILLEGAL_REQUEST,
			      OSD_ASC_INVALID_FIELD_IN_CDB, 0, 0);
	goto out;

out_sense:
	ret = sense_build_sdd(sense, OSD_SSK_HARDWARE_ERROR,
			      OSD_ASC_SYSTEM_RESOURCE_FAILURE, 0, 0);

out:
	free(root);
	return ret;
}

static inline int
mutiplex_getattr_list(struct osd_device *osd, uint64_t pid, uint64_t oid,
		      uint32_t page, uint32_t number, uint8_t *outbuf,
		      uint32_t outlen, uint8_t isembedded, uint8_t listfmt,
		      uint32_t *used_outlen, uint8_t *sense)
{
	if (page == GETALLATTR_PG && number == ATTRNUM_GETALL) {
		return attr_get_all_attrs(osd->dbc, pid, oid, outlen, outbuf,
					  listfmt, used_outlen);
	} else if (page != GETALLATTR_PG && number == ATTRNUM_GETALL) {
		return attr_get_page_as_list(osd->dbc, pid, oid, page, outlen,
					     outbuf, listfmt, used_outlen);
	} else if (page == GETALLATTR_PG && number != ATTRNUM_GETALL) {
		return attr_get_for_all_pages(osd->dbc, pid, oid, number,
					      outlen, outbuf, listfmt,
					      used_outlen);
	} else {
		return attr_get_attr(osd->dbc, pid, oid, page, number,
				     outlen, outbuf, listfmt, used_outlen);
	}
}


/*
 * Treatment of undefined attributes: Sec 7.1.3.1. The ver 2 draft changed
 * the semantics of handling undefined attributed from ver 1. Instead of
 * sending null attributes entry, list entry with attribute length 0 is sent
 * (Sec 7.1.1). If attr page is undefined then null attributes page is
 * returned (7.1.2.25)
 *
 * XXX:SD There is ambiguity wrt actions taken for "undefined page".  If the
 * "undefined page" means a page with no defined attributes, then a get all
 * attr on that page can either send a null attributes page (7.1.2.25) or one
 * can send list with list length 0 (7.1.3.1). We choose the latter option.
 *
 * XXX:SD Since we don't give names to pages when the pages are created
 * (doing so would increase complexity of set attr), when all attributes of a
 * page is requested we don't have attr num 0, which defines name of the
 * page. However, if the user has explicitly defined the name of the page,
 * then it would be returned if all the attributes of the page are requested.
 */
static int fill_null_attr(struct osd_device *osd, uint64_t pid, uint64_t oid,
			  uint32_t page, uint32_t number, uint8_t *outbuf,
			  uint32_t outlen, uint8_t listfmt)
{
	int ret;

	if (page != GETALLATTR_PG && number != ATTRNUM_GETALL) {
		if (listfmt == RTRVD_CREATE_MULTIOBJ_LIST)
			ret = le_multiobj_pack_attr(outbuf, outlen, oid,
						    page, number, 0, NULL);
		else
			ret = le_pack_attr(outbuf, outlen, page, number, 0,
					   NULL);
	} else {
		ret = 0;
	}

	return ret;
}

/*
 * XXX:SD We do lazy initialization of non-essential attributes to save time
 * on critical path for core operations like create.  Therefore if any of the
 * non-essential attributes are gotten then we need to set them before
 * accessing them.
 *
 * returns:
 * OSD_OK: success
 * OSD_ERROR: some error
 * -EINVAL: invalid arg in downstream function
 */
static int lazy_init_attr(struct osd_device *osd, uint64_t pid, uint64_t oid,
			  uint32_t page, uint32_t number)
{
	int ret;
	char val[40];
	uint32_t used_outlen;

	if ((page != GETALLATTR_PG && number != ATTRNUM_GETALL) ||
	    (page <= USER_ATOMICS_PG && number > 0) ||
	    (page >  USER_ATOMICS_PG && page != GETALLATTR_PG))
		return OSD_OK; /* nothing to be done */

	/* check if attrs are already defined */
	ret = attr_get_val(osd->dbc, pid, oid, USER_TMSTMP_PG, 0, 40, val,
			   &used_outlen);
	if (ret == OSD_OK)
		return OSD_OK; /* attrs already defined */
	else if (ret != -ENOENT)
		return ret;

	/* now initialize the attrs */
	ret = attr_set_attr(osd->dbc, pid, oid, USER_TMSTMP_PG, 0,
			    incits.user_tmstmp_page,
			    sizeof(incits.user_tmstmp_page));
	if (ret != OSD_OK)
		return ret;

	ret = attr_set_attr(osd->dbc, pid, oid, USER_ATOMICS_PG, 0,
			    incits.user_atomic_page,
			    sizeof(incits.user_atomic_page));
	if (ret != OSD_OK)
		return ret;

	return OSD_OK;
}

/*
 * returns:
 * == OSD_OK: success, used_outlen modified
 *  >0: failed, sense set accordingly
 */
int osd_getattr_list(struct osd_device *osd, uint64_t pid, uint64_t oid,
		     uint32_t page, uint32_t number, uint8_t *outbuf,
		     uint32_t outlen, uint8_t isembedded, uint8_t listfmt,
		     uint32_t *used_outlen, uint32_t cdb_cont_len, uint8_t *sense)
{
	int ret = 0;
	uint8_t obj_type = 0;

	assert(osd && osd->root && osd->dbc && outbuf && used_outlen && sense);

	obj_type = get_obj_type(osd, pid, oid);
	if (obj_type == ILLEGAL_OBJ)
		goto out_cdb_err;

	if (isgettable_page(obj_type, page) == false)
		goto out_param_list;

	ret = lazy_init_attr(osd, pid, oid, page, number);
	if (ret != OSD_OK)
		goto out_hw_err;

	switch (page) {
	case CUR_CMD_ATTR_PG:
		ret = get_ccap_aslist(osd, number, outbuf, outlen,
				      used_outlen);
		break;
	case PARTITION_DIR_PG + USER_TMSTMP_PG:
	case USER_TMSTMP_PG:
		ret = get_utsap_aslist(osd, pid, oid, number, outbuf,
				       outlen, listfmt, used_outlen);
		break;
	case PARTITION_DIR_PG + USER_QUOTA_PG:
	case PARTITION_DIR_PG + USER_INFO_PG:
	case USER_INFO_PG:
		ret = get_uiap(osd, pid, oid, page, number, outbuf,
			       outlen, listfmt, used_outlen);
		break;
	case ROOT_INFO_PG:
		ret = get_riap(osd, pid, oid, page, number, outbuf,
			       outlen, listfmt, used_outlen);
		break;
	default:
		ret = mutiplex_getattr_list(osd, pid, oid, page,
					    number, outbuf, outlen,
					    isembedded, listfmt,
					    used_outlen, sense);
		break;
	}

	assert (ret == -ENOENT || ret == -EIO || ret == -EINVAL ||
		ret == -ENOMEM || ret == OSD_ERROR || ret == OSD_OK);

	if (ret == -EIO || ret == -EINVAL || ret == -ENOMEM ||
	    ret == OSD_ERROR)
		goto out_param_list;

	if (ret == -ENOENT) {
		ret = fill_null_attr(osd, pid, oid, page, number, outbuf,
				     outlen, listfmt);
		assert(ret == -EINVAL || ret == -EOVERFLOW || ret >= 0);
		if (ret == -EOVERFLOW)
			*used_outlen = 0; /* not an error, Sec 5.2.2.2 */
		else if (ret >= 0)
			*used_outlen = ret;
		else
			goto out_param_list;
	}

	if (!isembedded)
		fill_ccap(&osd->ccap, NULL, obj_type, pid, oid, 0);
	return OSD_OK; /* success */

out_param_list:
	ret = sense_build_sdd(sense, OSD_SSK_ILLEGAL_REQUEST,
			      OSD_ASC_INVALID_FIELD_IN_PARAM_LIST,
			      pid, oid);
	return ret;

out_cdb_err:
	ret = sense_build_sdd(sense, OSD_SSK_ILLEGAL_REQUEST,
			      OSD_ASC_INVALID_FIELD_IN_CDB, pid, oid);
	return ret;

out_hw_err:
	ret = sense_build_sdd(sense, OSD_SSK_HARDWARE_ERROR,
			      OSD_ASC_INVALID_FIELD_IN_CDB, pid, oid);
	return ret;
}

/*
 * This function can only be used for pages that have a defined
 * format.  Those appear to be only:
 *    quotas:          root,partition,userobj
 *    timestamps:      root,partition,collection,userobj
 *    policy/security: root,partition,collection,userobj
 *    collection attributes: userobj (shows membership)
 *    current command
 *    null
 *
 * NOTE: since RTRVD_CREATE_MULTIOBJ_LIST listfmt can only be used when
 * cdbfmt == GETLIST_SETLIST, osd_getattr_page always generates list in
 * RTRVD_SET_ATTR_LIST. hence there is no listfmt arg.
 *
 * returns:
 * == OSD_OK: success, used_outlen modified
 *  >0: failed, sense set accordingly
 */
int osd_getattr_page(struct osd_device *osd, uint64_t pid, uint64_t oid,
		     uint32_t page, void *outbuf, uint64_t outlen,
		     uint8_t isembedded, uint32_t *used_outlen, 
		     uint32_t cdb_cont_len, uint8_t *sense)
{
	int ret = 0;
	uint8_t obj_type = 0;

	osd_debug("%s: get attr for (%llu, %llu) page %u", __func__,
		  llu(pid), llu(oid), page);

	assert(osd && osd->root && osd->dbc && outbuf && used_outlen && sense);

	obj_type = get_obj_type(osd, pid, oid);
	if (obj_type == ILLEGAL_OBJ)
		goto out_cdb_err;

	if (isgettable_page(obj_type, page) == false)
		goto out_param_list;

	if (!isembedded)
		fill_ccap(&osd->ccap, NULL, obj_type, pid, oid, 0);

	switch (page) {
	case CUR_CMD_ATTR_PG:
		ret = get_ccap(osd, outbuf, outlen, used_outlen);
		break;
	case USER_TMSTMP_PG:
		ret = get_utsap(osd, pid, oid, outbuf, outlen,
				used_outlen);
		break;
	case GETALLATTR_PG:
		ret = lazy_init_attr(osd, pid, oid, page, 0);
		if (ret != OSD_OK)
			goto out_hw_err;
		ret = attr_get_all_attrs(osd->dbc, pid, oid, outlen, outbuf,
					 RTRVD_SET_ATTR_LIST, used_outlen);
		break;
	default:
		goto out_cdb_err;
	}
	assert (ret == -EIO || ret == -EINVAL || ret == -ENOMEM ||
		ret == OSD_ERROR || ret == OSD_OK);
	if (ret == -EIO || ret == -EINVAL || ret == OSD_ERROR)
		goto out_param_list;

	return OSD_OK; /* success */

out_param_list:
	ret = sense_build_sdd(sense, OSD_SSK_ILLEGAL_REQUEST,
			      OSD_ASC_INVALID_FIELD_IN_PARAM_LIST,
			      pid, oid);
	return ret;

out_cdb_err:
	ret = sense_build_sdd(sense, OSD_SSK_ILLEGAL_REQUEST,
			      OSD_ASC_INVALID_FIELD_IN_CDB, pid, oid);
	return ret;

out_hw_err:
	ret = sense_build_sdd(sense, OSD_SSK_HARDWARE_ERROR,
			      OSD_ASC_INVALID_FIELD_IN_CDB, pid, oid);
	return ret;
}


int osd_get_member_attributes(struct osd_device *osd, uint64_t pid,
			      uint64_t cid, uint32_t cdb_cont_len, uint8_t *sense)
{
	osd_debug(__func__);
	return osd_error_unimplemented(0, sense);
}

/*
 * @outdata: pointer to start of the data-out-buffer: destination of
 * 	generated list results
 *
 * returns:
 * ==0: success, used_outlen is set
 * > 0: error, sense is set
 */
int osd_list(struct osd_device *osd, uint8_t list_attr, uint64_t pid,
	     uint64_t alloc_len, uint64_t initial_oid,
	     struct getattr_list *get_attr, uint32_t list_id,
	     uint8_t *outdata, uint64_t *used_outlen, uint8_t *sense)
{
	int ret = 0;
	uint8_t *cp = outdata;
	uint64_t add_len = 0;
	uint64_t cont_id = 0;

	assert(osd && osd->root && osd->dbc && get_attr && outdata 
	       && used_outlen && sense);

	if (alloc_len == 0)
		return 0; /* No space to even send the header */

	if (alloc_len < 24) /* XXX: currently need atleast the header */
		goto out_cdb_err;

	memset(outdata, 0, 24);

	if (list_attr == 0 && get_attr->sz != 0)
		goto out_cdb_err; /* XXX: unimplemented */
	if (list_attr == 1 && get_attr->sz == 0)
		goto out_cdb_err; /* XXX: this seems like error? */

	if (list_attr == 0 && get_attr->sz == 0)  {
		/*
		 * If list_id is not 0, we are continuing an old list,
		 * starting from cont_id
		 */
		if (list_id)
			initial_oid = cont_id;
		outdata[23] = (0x21 << 2);
		alloc_len -= 24;
		/*
		 * Looks like the process is identical except for the
		 * actual contents of the list, so this should work,
		 * unless we want attrs
		 */
		ret = (pid == 0 ?
		       obj_get_all_pids(osd->dbc, initial_oid, alloc_len,
					&outdata[24], used_outlen, &add_len,
					&cont_id)
		       :
		       obj_get_oids_in_pid(osd->dbc, pid, initial_oid,
					   alloc_len, &outdata[24],
					   used_outlen, &add_len, &cont_id)
		       );
		if (ret)
			goto out_hw_err;

		*used_outlen += 24;
		if (add_len + 16 > add_len) /* overflow: osd2r01 Sec 6.14.2 */
			add_len += 16;
		else
			add_len = (uint64_t) -1;
		set_htonll(outdata, add_len);
		set_htonll(&outdata[8], cont_id);
osd_error("%s: add_len=%lu cont_id=0x%lx", __func__, add_len, cont_id);
	} else if (list_attr == 1 && get_attr->sz != 0 && pid != 0) {
		if (list_id)
			initial_oid = cont_id;
		outdata[23] = (0x22 << 2);
		alloc_len -= 24;
		ret = mtq_list_oids_attr(osd->dbc, pid, initial_oid,
					 get_attr, alloc_len, &outdata[24],
					 used_outlen, &add_len, &cont_id);
		if (ret)
			goto out_hw_err;

		*used_outlen += 24;
		if (add_len + 16 > add_len) /* overflow: osd2r01 Sec 6.14.2 */
			add_len += 16;
		else
			add_len = (uint64_t) -1;
		set_htonll(outdata, add_len);
		set_htonll(&outdata[8], cont_id);
	}

	/* XXX: is this correct */
	fill_ccap(&osd->ccap, NULL, PARTITION, pid, PARTITION_OID, 0);
	return OSD_OK; /* success */

out_cdb_err:
	ret = sense_build_sdd(sense, OSD_SSK_ILLEGAL_REQUEST,
			      OSD_ASC_INVALID_FIELD_IN_CDB, pid, initial_oid);
	return ret;

out_hw_err:
	ret = sense_build_sdd(sense, OSD_SSK_HARDWARE_ERROR,
			      OSD_ASC_INVALID_FIELD_IN_CDB, pid, initial_oid);
	return ret;
}

/*
 * @outdata: pointer to start of the data-out-buffer: destination of
 * 	generated list results
 *
 * returns:
 * ==0: success, used_outlen is set
 * > 0: error, sense is set
 */
int osd_list_collection(struct osd_device *osd, uint8_t list_attr,
                        uint64_t pid, uint64_t cid, uint64_t alloc_len,
                        uint64_t initial_oid, struct getattr_list *get_attr,
                        uint32_t list_id, uint8_t *outdata,
                        uint64_t *used_outlen, uint8_t *sense)
{
	int ret = 0;
	uint8_t *cp = outdata;
	uint64_t add_len = 0;
	uint64_t cont_id = 0;

	assert(osd && osd->root && osd->dbc && get_attr && outdata 
	       && used_outlen && sense);

	if (alloc_len == 0)
		return 0;

	if (alloc_len < 24) /* XXX: currently need atleast the header */
		goto out_cdb_err;

	memset(outdata, 0, 24);

	if (list_attr == 0 && get_attr->sz != 0)
		goto out_cdb_err; /* XXX: unimplemented */
	if (list_attr == 1 && get_attr->sz == 0)
		goto out_cdb_err; /* XXX: this seems like error? */

	if (list_attr == 0 && get_attr->sz == 0)  {
		/*
		 * If list_id is not 0, we are continuing
		 * an old list, starting from cont_id
		 */
		if (list_id)
			initial_oid = cont_id;
		outdata[23] = (0x21 << 2);
		alloc_len -= 24;
		/*
		 * Looks like the process is identical except for the
		 * actual contents of the list, so this should work,
		 * unless we want attrs
		 */
		ret = (cid == 0 ?
		       obj_get_cids_in_pid(osd->dbc, pid, initial_oid,
					   alloc_len, &outdata[24],
					   used_outlen, &add_len, &cont_id)
		       :
		       coll_get_oids_in_cid(osd->dbc, pid, cid, initial_oid,
					    alloc_len, &outdata[24],
					    used_outlen, &add_len, &cont_id));
		if (ret)
			goto out_hw_err;
		*used_outlen += 24;
		add_len += 16;
		set_htonll(outdata, add_len);
		set_htonll(&outdata[8], cont_id);

	} else if (list_attr == 1 && get_attr->sz != 0 && cid != 0) {
		if (list_id)
			initial_oid = cont_id;
		goto out_cdb_err; /* XXX: unimplemented */
	}

	/* XXX: is this correct */
	fill_ccap(&osd->ccap, NULL, COLLECTION, pid, COLLECTION_OID_LB, 0);
	return OSD_OK; /* success */

out_cdb_err:
	ret = sense_build_sdd(sense, OSD_SSK_ILLEGAL_REQUEST,
			      OSD_ASC_INVALID_FIELD_IN_CDB, pid, initial_oid);
	return ret;

out_hw_err:
	ret = sense_build_sdd(sense, OSD_SSK_HARDWARE_ERROR,
			      OSD_ASC_INVALID_FIELD_IN_CDB, pid, initial_oid);
	return ret;
}

int osd_punch(struct osd_device *osd, uint64_t pid, uint64_t oid, uint64_t len,
	      uint64_t offset, uint32_t cdb_cont_len, uint8_t *sense)
{
	struct stat sb;       
        ssize_t readlen;
        int ret,fd=-1;
	uint64_t new_offset,new_len;
	char path[MAXNAMELEN];
	char *buf = NULL;
       
        osd_debug("%s: pid %llu oid %llu len %llu offset %llu", __func__, llu(pid),
                  llu(oid), llu(len), llu(offset));

	assert(osd && osd->root && osd->dbc && sense);

	if (!(pid >= USEROBJECT_PID_LB && oid >= USEROBJECT_OID_LB))	  
	        goto out_cdb_err;
	  
	get_dfile_name(path, osd->root, pid, oid);
	
	fd = open(path, O_RDWR|O_LARGEFILE); /* fails on non-existent obj */
	
	if (fd < 0)
	        goto out_cdb_err;

	new_offset = len + offset;	 
	
	ret = stat(path, &sb);
	
	if(ret != 0) {
	        close(fd);
	        return OSD_ERROR;
	}
	
	/* Handling Illegal Operation */
	if(offset > (uint64_t)sb.st_size)
	        goto out_cdb_err; 
	  
	/* Handling Special Case */
	else if(new_offset > (uint64_t)sb.st_size) {
	        ret = truncate(path, offset);
	        if (ret < 0)
		        goto out_hw_err;
	      	    
		ret = close(fd);
	    
		if (ret != 0)
		        goto out_hw_err;
		
		return OSD_OK;  /* success */
	}
	
	/* Regular Cases */
	new_len = sb.st_size - new_offset;

	buf = malloc(new_len);
	
	if(buf == NULL)
	        goto out_hw_err;
	  	
	/* Read section following the bytes to be removed */
	readlen = pread(fd, buf, new_len, new_offset);
	
	if (readlen < 0) 
	        goto out_hw_err;
	
	
	/* Overwrite the bytes to be removed and concatenate to new length */
	ret = pwrite(fd, buf, new_len, offset);
	
	if (ret < 0 || (uint64_t)ret != new_len)
	        goto out_hw_err;
	  	
	ret = truncate(path, offset + new_len);
	
	if (ret < 0)
	        goto out_hw_err;
	  
	ret = close(fd);
	
	if (ret != 0)
	        goto out_hw_err;
	  
	if (buf != NULL)
	        free(buf);
	  	
	return OSD_OK;  /* success */
	
 out_hw_err:
	ret = sense_build_sdd(sense, OSD_SSK_HARDWARE_ERROR,
			      OSD_ASC_INVALID_FIELD_IN_CDB, pid, oid);
	if(fd >= 0)
	        close(fd);
	 
	if(buf != NULL)
	        free(buf);

	return ret;
	
 out_cdb_err:
	ret = sense_build_sdd(sense, OSD_SSK_ILLEGAL_REQUEST,
			      OSD_ASC_INVALID_FIELD_IN_CDB, pid, oid);

	if(fd >= 0)
	        close(fd);
	  
	return ret;
}

static inline int alloc_qc(struct query_criteria *qc)
{
	uint32_t limit = 0;

	if (qc->qc_cnt_limit == 0)
		qc->qc_cnt_limit = 16;
	else
		qc->qc_cnt_limit *= 2;

	limit = qc->qc_cnt_limit;
	qc->qce_len = realloc(qc->qce_len, sizeof(*(qc->qce_len))*limit);
	qc->page = realloc(qc->page, sizeof(*(qc->page))*limit);
	qc->number = realloc(qc->number, sizeof(*(qc->number))*limit);
	qc->min_len = realloc(qc->min_len, sizeof(*(qc->min_len))*limit);
	qc->min_val = realloc(qc->min_val, sizeof(void *)*limit);
	qc->max_len = realloc(qc->max_len, sizeof(*(qc->max_len))*limit);
	qc->max_val = realloc(qc->max_val, sizeof(void *)*limit);

	if (!qc->qce_len || !qc->page || !qc->number || !qc->min_len ||
	    !qc->min_val || !qc->max_len || !qc->max_val)
		return -ENOMEM;

	return OSD_OK;
}

static inline void free_qc(struct query_criteria *qc)
{
	if (!qc)
		return;
	free(qc->qce_len);
	free(qc->page);
	free(qc->number);
	free(qc->min_len);
	free(qc->min_val);
	free(qc->max_len);
	free(qc->max_val);
}

static int parse_query_criteria(const uint8_t *cp, uint32_t qll,
				struct query_criteria *qc)
{
	int ret = 0;

	qc->query_type = cp[0] & 0xF;
	qc->qc_cnt = 0;
	cp += 4;
	qll -= 4; /* remove query list header */
	while (qll > 0) {
		qc->qce_len[qc->qc_cnt] = get_ntohs(&cp[2]);
		if (qc->qce_len[qc->qc_cnt] == MINQCELEN)
			break; /* qce is empty */

		qc->page[qc->qc_cnt] = get_ntohl(&cp[4]);
		if (qc->page[qc->qc_cnt] >= PARTITION_PG) /* osd2r01 p 120 */
			return OSD_ERROR;

		qc->number[qc->qc_cnt] = get_ntohl(&cp[8]);
		qc->min_len[qc->qc_cnt] = get_ntohs(&cp[12]);
		qc->min_val[qc->qc_cnt] = &cp[14];
		cp += (14 + qc->min_len[qc->qc_cnt]);
		qll -= (14 + qc->min_len[qc->qc_cnt]);
		qc->max_len[qc->qc_cnt] = get_ntohs(&cp[0]);
		qc->max_val[qc->qc_cnt] = &cp[2];
		cp += (2 + qc->max_len[qc->qc_cnt]);
		qll -= (2 + qc->max_len[qc->qc_cnt]);
		qc->qc_cnt++;

		if (qc->qc_cnt == qc->qc_cnt_limit) {
			ret = alloc_qc(qc);
			if (ret != OSD_OK)
				return ret;
		}
	}

	return OSD_OK;
}

int osd_query(struct osd_device *osd, uint64_t pid, uint64_t cid,
	      uint32_t query_list_len, uint64_t alloc_len, const void *indata,
	      void *outdata, uint64_t *used_outlen, uint32_t cdb_cont_len, uint8_t *sense)
{
	int ret = 0;
	int present = 0;
	uint8_t *cp = outdata;
	struct query_criteria qc = {
		.query_type = 0,
		.qc_cnt_limit = 0,
		.qc_cnt = 0,
		.qce_len = NULL,
		.page = NULL,
		.number = NULL,
		.min_len = NULL,
		.min_val = NULL,
		.max_len = NULL,
		.max_val = NULL,
	};

	osd_debug("%s pid %llu cid %llu query_list_len %u alloc_len %llu",
		  __func__, llu(pid), llu(cid), query_list_len,
		  llu(alloc_len));

	assert(osd && osd->root && osd->dbc && indata && sense);

	if (pid < USEROBJECT_PID_LB)
		goto out_cdb_err;

	ret = obj_ispresent(osd->dbc, pid, cid, &present);
	if (ret != OSD_OK || !present)
		goto out_cdb_err;

	if (query_list_len < MINQLISTLEN)
		goto out_cdb_err;

	if (alloc_len == 0)
		return OSD_OK;
	if (alloc_len < MIN_ML_LEN)
		goto out_cdb_err;

	ret = alloc_qc(&qc);
	if (ret != OSD_OK)
		goto out_hw_err;

	ret = parse_query_criteria(indata, query_list_len, &qc);
	if (ret != OSD_OK)
		goto out_cdb_err;

	memset(cp+8, 0, 4);  /* reserved area */
	cp[12] = (0x21 << 2);
	ret = mtq_run_query(osd->dbc, pid, cid, &qc, outdata, alloc_len,
			    used_outlen);
	free_qc(&qc);
	if (ret != OSD_OK)
		goto out_hw_err;

	fill_ccap(&osd->ccap, NULL, COLLECTION, pid, cid, 0);
	return OSD_OK; /* success */

out_hw_err:
	ret = sense_build_sdd(sense, OSD_SSK_HARDWARE_ERROR,
			      OSD_ASC_INVALID_FIELD_IN_CDB, pid, cid);
	return ret;

out_cdb_err:
	ret = sense_build_sdd(sense, OSD_SSK_ILLEGAL_REQUEST,
			      OSD_ASC_INVALID_FIELD_IN_CDB, pid, cid);
	return ret;
}

/*
 * @offset: offset from byte zero of the object where data will be read
 * @len: length of data to be read
 * @outdata: pointer to start of the data-out-buffer: destination of read
 *
 * returns:
 * ==0: success, used_outlen is set
 * > 0: error, sense is set
 */
static int contig_read(struct osd_device *osd, uint64_t pid, uint64_t oid, 
		       uint64_t len, uint64_t offset, uint8_t *outdata, 
		       uint64_t *used_outlen, uint8_t *sense)
{
	ssize_t readlen;
	int ret, fd;
	char path[MAXNAMELEN];

	osd_debug("%s: pid %llu oid %llu len %llu offset %llu", __func__,
		  llu(pid), llu(oid), llu(len), llu(offset));

	assert(osd && osd->root && osd->dbc && outdata && used_outlen 
	       && sense);

	if (!(pid >= USEROBJECT_PID_LB && oid >= USEROBJECT_OID_LB))
		goto out_cdb_err;

	get_dfile_name(path, osd->root, pid, oid);
	fd = open(path, O_RDONLY|O_LARGEFILE); /* fails on non-existent obj */
	if (fd < 0) {
		osd_error("%s: open faild on [%s]", __func__, path);
		goto out_cdb_err;
	}

	readlen = pread(fd, outdata, len, offset);
	ret = close(fd);
	if ((readlen < 0) || (ret != 0))
		goto out_hw_err;

	/* valid, but return a sense code */
	if ((size_t) readlen < len) {
		memset(outdata + readlen, 0, len - readlen);
		ret = sense_build_sdd_csi(sense, OSD_SSK_RECOVERED_ERROR,
				      OSD_ASC_READ_PAST_END_OF_USER_OBJECT,
				      pid, oid, readlen);
	}

	*used_outlen = len;

	fill_ccap(&osd->ccap, NULL, USEROBJECT, pid, oid, 0);
	return ret;

out_hw_err:
	ret = sense_build_sdd(sense, OSD_SSK_HARDWARE_ERROR,
			      OSD_ASC_INVALID_FIELD_IN_CDB, pid, oid);
	return ret;

out_cdb_err:
	ret = sense_build_sdd(sense, OSD_SSK_ILLEGAL_REQUEST,
			      OSD_ASC_INVALID_FIELD_IN_CDB, pid, oid);
	return ret;


}

static int sgl_read(struct osd_device *osd, uint64_t pid, uint64_t oid, 
		    uint64_t len, uint64_t offset, const struct sg_list *sglist,
		    uint8_t *outdata, uint64_t *used_outlen, uint8_t *sense)
{
	ssize_t readlen;
	int ret, fd;
	char path[MAXNAMELEN];
	uint64_t inlen, pairs, offset_val, data_offset, length;
	unsigned int i;

	osd_debug("%s: pid %llu oid %llu len %llu offset %llu", __func__,
		  llu(pid), llu(oid), llu(len), llu(offset));

	assert(osd && osd->root && osd->dbc && outdata && used_outlen 
	       && sense);

	pairs = sglist->num_entries;
	inlen = (pairs * sizeof(uint64_t) * 2) + sizeof(uint64_t);
	assert(pairs * sizeof(uint64_t) * 2 == inlen - sizeof(uint64_t));

	osd_debug("%s: offset,len pairs %llu", __func__, llu(pairs));
	osd_debug("%s: data offset %llu", __func__, llu(inlen));


	if (!(pid >= USEROBJECT_PID_LB && oid >= USEROBJECT_OID_LB))
		goto out_cdb_err;

	get_dfile_name(path, osd->root, pid, oid);
	fd = open(path, O_RDONLY|O_LARGEFILE); /* fails on non-existent obj */
	if (fd < 0)
		goto out_cdb_err;


	data_offset = 0;
	readlen = 0;

	for (i = 0; i < pairs; i++) {
		/* offset into dest */
		offset_val = get_ntohll(&sglist->entries[i].offset);
		length = get_ntohll(&sglist->entries[i].bytes_to_transfer);

		osd_debug("%s: Offset: %llu Length: %llu", __func__, llu(offset_val + offset),
			llu(length));

		osd_debug("%s: Position in data buffer: %llu master offset %llu", __func__, llu(data_offset), llu(offset));

		osd_debug("%s: ------------------------------", __func__);
		ret = pread(fd, outdata+data_offset, length, offset_val+offset);
		osd_debug("%s: return value is %d", __func__, ret);
		if (ret < 0)
			goto out_hw_err;
		if ((size_t) ret < length) {
			/* valid, fill with zeros */
			memset(outdata+data_offset+ret, 0, length - ret);
			length = ret;
		}
		data_offset += length;
		readlen += length;
	}

	ret = close(fd);
	if (ret != 0)
		goto out_hw_err;

	*used_outlen = readlen;

	/* valid, but return a sense code */
	if ((size_t) readlen < len)
		ret = sense_build_sdd_csi(sense, OSD_SSK_RECOVERED_ERROR,
				      OSD_ASC_READ_PAST_END_OF_USER_OBJECT,
				      pid, oid, readlen);

	fill_ccap(&osd->ccap, NULL, USEROBJECT, pid, oid, 0);
	return ret;

out_hw_err:
	ret = sense_build_sdd(sense, OSD_SSK_HARDWARE_ERROR,
			      OSD_ASC_INVALID_FIELD_IN_CDB, pid, oid);
	return ret;

out_cdb_err:
	ret = sense_build_sdd(sense, OSD_SSK_ILLEGAL_REQUEST,
			      OSD_ASC_INVALID_FIELD_IN_CDB, pid, oid);
	return ret;


}

static int vec_read(struct osd_device *osd, uint64_t pid, uint64_t oid, 
		    uint64_t len, uint64_t offset, const uint8_t *indata,
		    uint8_t *outdata, uint64_t *used_outlen, uint8_t *sense)
{
	ssize_t readlen;
	int ret, fd;
	char path[MAXNAMELEN];
	uint64_t inlen, bytes, hdr_offset, offset_val, data_offset, length, stride;
	unsigned int i;

	osd_debug("%s: pid %llu oid %llu len %llu offset %llu", __func__,
		  llu(pid), llu(oid), llu(len), llu(offset));

	assert(osd && osd->root && osd->dbc && outdata && used_outlen 
	       && sense);

	stride = get_ntohll(indata);
	hdr_offset = sizeof(uint64_t);
	length = get_ntohll(indata + hdr_offset);

	osd_debug("%s: stride is %llu and len is %llu", __func__, llu(stride), 
		  llu(length));

	if (!(pid >= USEROBJECT_PID_LB && oid >= USEROBJECT_OID_LB))
		goto out_cdb_err;

	get_dfile_name(path, osd->root, pid, oid);
	fd = open(path, O_RDONLY|O_LARGEFILE); /* fails on non-existent obj */
	if (fd < 0)
		goto out_cdb_err;

	data_offset = 0;
	bytes = len;
	readlen = 0;
	osd_debug("%s: bytes to read is %llu", __func__, llu(bytes));
	offset_val = 0;
	while (bytes > 0) {
		osd_debug("%s: Position in data buffer: %llu", __func__,
			   llu(data_offset));
		osd_debug("%s: Offset: %llu", __func__, llu(offset_val + offset));
		osd_debug("%s: ------------------------------", __func__);
		ret = pread(fd, outdata+data_offset, length, offset_val+offset);
		if (ret < 0 || (uint64_t)ret != length)
			goto out_hw_err;
		readlen += ret;
		data_offset += length;
		offset_val += stride;
		bytes -= length;
		if (bytes < length)
			length = bytes;
		osd_debug("%s: Total Bytes Left to read: %llu", __func__,
			  llu(bytes));
	}

	ret = close(fd);
	if (ret != 0)
		goto out_hw_err;

	*used_outlen = readlen;

	/* valid, but return a sense code */
	if ((size_t) readlen < len)
		ret = sense_build_sdd_csi(sense, OSD_SSK_RECOVERED_ERROR,
				      OSD_ASC_READ_PAST_END_OF_USER_OBJECT,
				      pid, oid, readlen);

	fill_ccap(&osd->ccap, NULL, USEROBJECT, pid, oid, 0);
	return ret;

out_hw_err:
	ret = sense_build_sdd(sense, OSD_SSK_HARDWARE_ERROR,
			      OSD_ASC_INVALID_FIELD_IN_CDB, pid, oid);
	return ret;

out_cdb_err:
	ret = sense_build_sdd(sense, OSD_SSK_ILLEGAL_REQUEST,
			      OSD_ASC_INVALID_FIELD_IN_CDB, pid, oid);
	return ret;


}


int osd_read(struct osd_device *osd, uint64_t pid, uint64_t oid, uint64_t len,
	     uint64_t offset, const uint8_t *indata, uint8_t *outdata,
	     uint64_t *used_outlen, const struct sg_list *sglist,
	     uint8_t *sense, uint8_t ddt)
{
	/*figure out what kind of write it is based on ddt and call appropriate
	write function*/

	switch(ddt) {
		case DDT_CONTIG: {
			return contig_read(osd, pid, oid, len, offset, outdata,
					used_outlen, sense);
		}
		case DDT_SGL: {
			return sgl_read(osd, pid, oid, len, offset, sglist,
				outdata, used_outlen, sense);
		}
		case DDT_VEC: {
			return vec_read(osd, pid, oid, len, offset, indata,
				outdata, used_outlen, sense);
		}
		default: {
			return sense_build_sdd(sense, OSD_SSK_ILLEGAL_REQUEST,
			               OSD_ASC_INVALID_FIELD_IN_CDB, pid, oid);
		}
	}

}

static inline int set_dscptr(uint16_t dscptr_type, uint32_t data_len, uint64_t byte_offset,
			     uint8_t *offset)
{
        set_htons(offset+2, dscptr_type);
	set_htonl(offset+4, data_len);
	set_htonll(offset+8, byte_offset);
	return 0;
}

/* This implementation assumes map_type = WRITTEN_DATA */
/* Returns a map of the data and attributes in the specified user object */
int osd_read_map(struct osd_device *osd, uint64_t pid, uint64_t oid, uint64_t alloc_len,
		 uint64_t offset, uint16_t map_type, uint8_t *outdata, uint64_t *used_outlen,
		 uint32_t cdb_cont_len, uint8_t *sense)
{
	int ret, fd = -1, i;
	uint64_t dscptr_size = 0x00000004;  /*set to 4 for testing purpose, change to 0xffffffff */
	char path[MAXNAMELEN];
	uint8_t *pt;
	uint64_t  byte_offset, file_size, add_len = 0;
	uint32_t data_len;
       	struct stat sb;

	osd_debug("%s: pid %llu oid %llu alloc_len %llu offset %llu", __func__,
		  llu(pid), llu(oid), llu(alloc_len), llu(offset));

	assert(osd && osd->root && osd->dbc && sense && outdata);

	if (alloc_len == 0)
	        return 0; /* No data shall be transfered */

	else if (alloc_len < 24) /* alloc_len needs to be at least 24 for one descriptor */
		goto out_cdb_err;

	memset(outdata, 0, alloc_len);

	if (!(pid >= USEROBJECT_PID_LB && oid >= USEROBJECT_OID_LB))
		goto out_cdb_err;

	get_dfile_name(path, osd->root, pid, oid);
	fd = open(path, O_RDONLY|O_LARGEFILE); /* fails on non-existent obj */
	if (fd < 0)
		goto out_cdb_err;
		
	ret = stat(path, &sb);
	
	if (ret != 0) {
	        close(fd);
		return OSD_ERROR;
	}
	
	if (offset > (uint64_t)sb.st_size)
	        goto out_cdb_err; 

	file_size = (uint64_t)sb.st_size - offset; /* Adjust to the offset */

	if (map_type == WRITTEN_DATA) { 
  
		pt = outdata + 8; /* Skip addtional length for now */
		byte_offset = 8;
		
		while(file_size > 0) { 
		        if(file_size <= dscptr_size) { /* one descriptor */
		                data_len = file_size;
				file_size = 0;
			
			}else {
			        data_len = dscptr_size;
			        file_size -= dscptr_size;
			}
						
			set_dscptr(map_type, data_len, byte_offset, pt);
			byte_offset += 16;
			pt += 16;
			add_len++;
		}
		
		*used_outlen = 8 + (add_len * 16); /* Report total buffer len used */
		
		if((add_len * 16) > 0xffffffff )
		        set_htonll(outdata, 0xffffffff);
		else
		        set_htonll(outdata, (add_len * 16)); /* Set additional length */

	}else if (map_type == DATA_HOLE) {
	        osd_debug(__func__);
		return osd_error_unimplemented(0, sense);
	}else if (map_type == DAMAGED_DATA) {
	 	osd_debug(__func__);
		return osd_error_unimplemented(0, sense);
	}else if (map_type == DAMAGED_ATTRIBUTES) {
		osd_debug(__func__);
		return osd_error_unimplemented(0, sense);
	}else { /* map_type == ALL_DATA */
	  	osd_debug(__func__);
		return osd_error_unimplemented(0, sense);
	}
	
	ret = close(fd);
	if (ret)
		goto out_hw_err;

	return OSD_OK; /* success */

out_hw_err:
	ret = sense_build_sdd(sense, OSD_SSK_HARDWARE_ERROR,
			      OSD_ASC_INVALID_FIELD_IN_CDB, pid, oid);
	return ret;

out_cdb_err:
	ret = sense_build_sdd(sense, OSD_SSK_ILLEGAL_REQUEST,
			      OSD_ASC_INVALID_FIELD_IN_CDB, pid, oid);
	return ret;

}

int osd_remove(struct osd_device *osd, uint64_t pid, uint64_t oid,
	       uint32_t cdb_cont_len, uint8_t *sense)
{
	int ret = 0;
	char path[MAXNAMELEN];

	osd_debug("%s: removing userobject pid %llu oid %llu", __func__,
		  llu(pid), llu(oid));

	assert(osd && osd->root && osd->dbc && sense);

	if (!(pid >= USEROBJECT_PID_LB && oid >= USEROBJECT_OID_LB))
		goto out_cdb_err;

	/* XXX: invalidate ic_cache immediately */
	osd->ic.cur_pid = osd->ic.next_id = 0;

	/* if userobject is absent unlink will fail */
	get_dfile_name(path, osd->root, pid, oid);
	ret = unlink(path);
	if (ret != 0)
		goto out_hw_err;

	/* delete all attr of the object */
	ret = attr_delete_all(osd->dbc, pid, oid);
	if (ret != 0)
		goto out_hw_err;

	/* delete all collection memberships */
	ret = coll_delete_oid(osd->dbc, pid, oid);
	if (ret != 0)
		goto out_hw_err;

	ret = obj_delete(osd->dbc, pid, oid);
	if (ret != 0)
		goto out_hw_err;

	fill_ccap(&osd->ccap, NULL, USEROBJECT, pid, oid, 0);
	return OSD_OK; /* success */

out_cdb_err:
	ret = sense_build_sdd(sense, OSD_SSK_ILLEGAL_REQUEST,
			      OSD_ASC_INVALID_FIELD_IN_CDB, pid, oid);
	return ret;

out_hw_err:
	ret = sense_build_sdd(sense, OSD_SSK_HARDWARE_ERROR,
			      OSD_ASC_INVALID_FIELD_IN_CDB, pid, oid);
	return ret;
}


int osd_remove_collection(struct osd_device *osd, uint64_t pid, uint64_t cid,
			  uint8_t fcr, uint32_t cdb_cont_len, uint8_t *sense)
{
	int ret = 0;
	int isempty = 0;
	int present = 0;

	osd_debug("%s: pid %llu cid %llu fcr %u", __func__, llu(pid),
		  llu(cid), fcr);

	assert(osd && osd->root && osd->dbc && sense);

	if (pid == 0 || pid < COLLECTION_PID_LB)
		goto out_cdb_err;

	if (cid < COLLECTION_OID_LB)
		goto out_cdb_err;

	/* make sure collection object is present */
	ret = obj_ispresent(osd->dbc, pid, cid, &present);
	if (ret != OSD_OK || !present)
		goto out_cdb_err;

	/* XXX: invalidate ic_cache */
	osd->ic.cur_pid = osd->ic.next_id = 0;

	ret = coll_isempty_cid(osd->dbc, pid, cid, &isempty);
	if (ret != OSD_OK)
		goto out_hw_err;

	if (!isempty) {
		if (fcr == 0)
			goto out_not_empty;

		ret = coll_delete_cid(osd->dbc, pid, cid);
		if (ret != 0)
			goto out_hw_err;
	}

	ret = attr_delete_all(osd->dbc, pid, cid);
	if (ret != 0)
		goto out_hw_err;

	ret = obj_delete(osd->dbc, pid, cid);
	if (ret != 0)
		goto out_hw_err;

	fill_ccap(&osd->ccap, NULL, COLLECTION, pid, cid, 0);
	return OSD_OK; /* success */

out_cdb_err:
	ret = sense_build_sdd(sense, OSD_SSK_ILLEGAL_REQUEST,
			      OSD_ASC_INVALID_FIELD_IN_CDB, pid, cid);
	return ret;

out_not_empty:
	ret = sense_build_sdd(sense, OSD_SSK_ILLEGAL_REQUEST,
			      OSD_ASC_PART_OR_COLL_CONTAINS_USER_OBJECTS,
			      pid, cid);
	return ret;

out_hw_err:
	ret = sense_build_sdd(sense, OSD_SSK_HARDWARE_ERROR,
			      OSD_ASC_INVALID_FIELD_IN_CDB, pid, cid);
	return ret;
}


int osd_remove_member_objects(struct osd_device *osd, uint64_t pid,
			      uint64_t cid, uint32_t cdb_cont_len, uint8_t *sense)
{
	osd_debug(__func__);
	return osd_error_unimplemented(0, sense);
}

/*
 * returns:
 * ==0: OSD_OK on success
 *  >0: error, sense set approprirately
 */
int osd_remove_partition(struct osd_device *osd, uint64_t pid, uint32_t cdb_cont_len,
			 uint8_t *sense)
{
	int ret = 0;
	int isempty = 0;

	osd_debug("%s: pid %llu", __func__, llu(pid));

	assert(osd && osd->root && osd->dbc && sense);

	if (pid == 0)
		goto out_cdb_err;

	ret = obj_isempty_pid(osd->dbc, pid, &isempty);
	if (ret != OSD_OK || !isempty)
		goto out_not_empty;

	/* XXX: invalidate ic_cache */
	osd->ic.cur_pid = osd->ic.next_id = 0;

	ret = attr_delete_all(osd->dbc, pid, PARTITION_OID);
	if (ret != 0)
		goto out_err;

	ret = obj_delete(osd->dbc, pid, PARTITION_OID);
	if (ret != 0)
		goto out_err;

	fill_ccap(&osd->ccap, NULL, PARTITION, pid, PARTITION_OID, 0);
	return OSD_OK; /* success */

out_cdb_err:
	ret = sense_build_sdd(sense, OSD_SSK_ILLEGAL_REQUEST,
			      OSD_ASC_INVALID_FIELD_IN_CDB, pid,
			      PARTITION_OID);
	return ret;

out_err:
	ret = sense_build_sdd(sense, OSD_SSK_HARDWARE_ERROR,
			      OSD_ASC_INVALID_FIELD_IN_CDB, pid,
			      PARTITION_OID);
	return ret;

out_not_empty:
	ret = sense_build_sdd(sense, OSD_SSK_ILLEGAL_REQUEST,
			      OSD_ASC_PART_OR_COLL_CONTAINS_USER_OBJECTS,
			      pid, PARTITION_OID);
	return ret;
}


/*
 * Settable page numbers in any given U,P,C,R range are further restricted
 * by osd2r00 p 23:  >= 0x10000 && < 0x20000000.
 *
 * -	XXX: attr directory setting
 */
int osd_set_attributes(struct osd_device *osd, uint64_t pid, uint64_t oid,
		       uint32_t page, uint32_t number, const void *val,
		       uint16_t len, uint8_t isembedded, uint32_t cdb_cont_len, 
		       uint8_t *sense)
{
	int ret = 0;
	int present = 0;
	uint8_t obj_type = 0;

#ifdef PVFS_OSD_INTEGRATED
	if (page == USER_INFO_PG && number == UIAP_LOGICAL_LEN) {
		ret = set_uiap(osd, pid, oid, number, val, len);
		if (ret == OSD_OK)
			goto out_success;
		else
			goto out_cdb_err;
	}
#endif

/*	osd_debug("%s: set attr on pid %llu oid %llu", __func__, llu(pid),
		  llu(oid)); */

	assert(osd && osd->root && osd->dbc && sense);

	ret = obj_ispresent(osd->dbc, pid, oid, &present);
	if (ret != OSD_OK || !present) {/* object not present! */
		osd_warning("%s: object not present pid %llu oid %llu", __func__,
			  llu(pid), llu(oid));
		goto out_cdb_err;
	}

	obj_type = get_obj_type(osd, pid, oid);
	if (obj_type == ILLEGAL_OBJ) {
		osd_warning("%s: !get_obj_type pid %llu oid %llu", __func__,
			  llu(pid), llu(oid));
		goto out_cdb_err;
	}

	if (issettable_page(obj_type, page) == false)
		goto out_param_list;

	if (number == ATTRNUM_UNMODIFIABLE)
		goto out_param_list;

	if ((val == NULL && len != 0) || (val != NULL && len == 0)) {
		osd_warning("%s: NULLs %llu oid %llu", __func__,
			  llu(pid), llu(oid));
		goto out_cdb_err;
	}

	/* information page, make sure null terminated. osd2r00 7.1.2.2 */
	if (number == ATTRNUM_INFO) {
		int i;
		const uint8_t *s = val;

		if (len > ATTR_PAGE_ID_LEN)
			goto out_cdb_err;
		for (i=0; i<len; i++) {
			if (s[i] == 0)
				break;
		}
		if (i == len) {
			osd_warning("%s: !null terminated %llu oid %llu",
				  __func__, llu(pid), llu(oid));
			goto out_cdb_err;
		}
	}

	if (len > ATTR_LEN_UB)
		goto out_param_list;

	switch (page) {
	case USER_INFO_PG:
		ret = set_uiap(osd, pid, oid, number, val, len);
		if (ret == OSD_OK)
			goto out_success;
		else
			goto out_cdb_err;
	case ROOT_INFO_PG:
		ret = set_riap(osd, pid, oid, number, val, len);
		if (ret == OSD_OK)
			goto out_success;
		else
			goto out_cdb_err;
	case USER_COLL_PG:
		ret = set_cap(osd, pid, oid, number, val, len);
		if (ret == OSD_OK)
			goto out_success;
		else
			goto out_cdb_err;
	default:
		break;
	}


	/*
	 * XXX:SD len == 0 is equivalent to deleting the attr. osd2r00 4.7.4
	 * second last paragraph. only attributes with non zero length are
	 * retrieveable
	 */
	if (len == 0) {
		ret = attr_delete_attr(osd->dbc, pid, oid, page, number);
		if (ret == 0)
			goto out_success;
		else
			goto out_cdb_err;
	}

	ret = attr_set_attr(osd->dbc, pid, oid, page, number, val, len);
	if (ret != 0)
		goto out_hw_err;

out_success:
	if (!isembedded)
		fill_ccap(&osd->ccap, NULL, obj_type, pid, oid, 0);
	return OSD_OK; /* success */

out_param_list:
	return sense_build_sdd(sense, OSD_SSK_ILLEGAL_REQUEST,
			       OSD_ASC_INVALID_FIELD_IN_PARAM_LIST,
			       pid, oid);

out_cdb_err:
	return sense_build_sdd(sense, OSD_SSK_ILLEGAL_REQUEST,
			      OSD_ASC_INVALID_FIELD_IN_CDB, pid, oid);
out_hw_err:
	return sense_build_sdd(sense, OSD_SSK_HARDWARE_ERROR,
			       OSD_ASC_INVALID_FIELD_IN_CDB, pid, oid);
}


int osd_set_key(struct osd_device *osd, int key_to_set, uint64_t pid,
		uint64_t key, uint8_t seed[20], uint8_t *sense)
{
	osd_debug(__func__);
	return osd_error_unimplemented(0, sense);
}


int osd_set_master_key(struct osd_device *osd, int dh_step, uint64_t key,
		       uint32_t param_len, uint32_t alloc_len,
		       uint8_t *outdata, uint64_t *outlen, uint32_t cdb_cont_len,
		       uint8_t *sense)
{
	osd_debug(__func__);
	return osd_error_unimplemented(0, sense);
}


int osd_set_member_attributes(struct osd_device *osd, uint64_t pid,
			      uint64_t cid, struct setattr_list *set_attr,
			      uint32_t cdb_cont_len, uint8_t *sense)
{
	int ret = 0;
	size_t i = 0;
	int present = 0;
	uint8_t obj_type = 0;
	uint8_t within_txn = 0;


	osd_debug("%s: set attrs on pid %llu cid %llu", __func__, llu(pid),
		  llu(cid));

	assert(osd && osd->root && osd->dbc && set_attr && sense);

	/* encapsulate all db ops in txn */
	ret = db_begin_txn(osd->dbc);
	assert(ret == 0);
	within_txn = 1;

	ret = obj_ispresent(osd->dbc, pid, cid, &present);
	if (ret != OSD_OK || !present) /* collection absent! */
		goto out_cdb_err;

	obj_type = get_obj_type(osd, pid, cid);
	if (obj_type != COLLECTION)
		goto out_cdb_err;

	/*
	 * XXX: presently we only allow attrs to modify useobjects, not the
	 * collection
	 */
	for (i = 0; i < set_attr->sz; i++) {
		if (!issettable_page(USEROBJECT, set_attr->le[i].page))
			goto out_param_list;
	}

	ret = mtq_set_member_attrs(osd->dbc, pid, cid, set_attr);
	if (ret != 0)
		goto out_hw_err;

	ret = db_end_txn(osd->dbc);
	assert(ret == 0);

	fill_ccap(&osd->ccap, NULL, COLLECTION, pid, cid, 0);
	return OSD_OK; /* success */

out_hw_err:
	if (within_txn) {
		ret = db_end_txn(osd->dbc);
		assert(ret == 0);
	}
	return sense_build_sdd(sense, OSD_SSK_HARDWARE_ERROR,
			       OSD_ASC_INVALID_FIELD_IN_CDB, pid, cid);

out_param_list:
	if (within_txn) {
		ret = db_end_txn(osd->dbc);
		assert(ret == 0);
	}
	return sense_build_sdd(sense, OSD_SSK_ILLEGAL_REQUEST,
			       OSD_ASC_INVALID_FIELD_IN_PARAM_LIST, pid, cid);

out_cdb_err:
	if (within_txn) {
		ret = db_end_txn(osd->dbc);
		assert(ret == 0);
	}
	return sense_build_sdd(sense, OSD_SSK_ILLEGAL_REQUEST,
			       OSD_ASC_INVALID_FIELD_IN_CDB, pid, cid);
}


/*
 * TODO: We did not implement length as an attribute in attr table, hence
 * relying on underlying ext3 fs to get length of the object. Also since we
 * are not implementing quotas, we ignore maximum length attribute of an
 * object and the partition which stores the object.
 *
 * @offset: offset from byte zero of the object where data will be written
 * @len: length of data to be written
 * @dinbuf: pointer to start of the Data-in-buffer: source of data
 */

static int contig_write(struct osd_device *osd, uint64_t pid, uint64_t oid, 
			uint64_t len, uint64_t offset, const uint8_t *dinbuf, 
			uint8_t *sense)
{
	int ret;
	int fd;
	char path[MAXNAMELEN];

	osd_debug("%s: pid %llu oid %llu len %llu offset %llu data %p",
		  __func__, llu(pid), llu(oid), llu(len), llu(offset), dinbuf);

	assert(osd && osd->root && osd->dbc && dinbuf && sense);

	if (!(pid >= USEROBJECT_PID_LB && oid >= USEROBJECT_OID_LB))
		goto out_cdb_err;

	get_dfile_name(path, osd->root, pid, oid);
	fd = open(path, O_RDWR|O_LARGEFILE); /* fails on non-existent obj */
	if (fd < 0)
		goto out_cdb_err;

	ret = pwrite(fd, dinbuf, len, offset);
	if (ret < 0 || (uint64_t)ret != len)
		goto out_hw_err;
	ret = close(fd);
	if (ret != 0)
		goto out_hw_err;

	fill_ccap(&osd->ccap, NULL, USEROBJECT, pid, oid, 0);
	return OSD_OK; /* success */

out_hw_err:
	ret = sense_build_sdd(sense, OSD_SSK_HARDWARE_ERROR,
			      OSD_ASC_INVALID_FIELD_IN_CDB, pid, oid);
	return ret;

out_cdb_err:
	ret = sense_build_sdd(sense, OSD_SSK_ILLEGAL_REQUEST,
			      OSD_ASC_INVALID_FIELD_IN_CDB, pid, oid);
	return ret;


}

static int sgl_write(struct osd_device *osd, uint64_t pid, uint64_t oid, 
		     uint64_t len, uint64_t offset, const uint8_t *dinbuf,
		     const struct sg_list *sglist,
		     uint8_t *sense) 
{
	int ret;
	int fd;
	char path[MAXNAMELEN];
	uint64_t pairs, data_offset, offset_val, length;
	unsigned int i;

	osd_info("%s: pid %llu oid %llu len %llu offset %llu data %p",
		  __func__, llu(pid), llu(oid), llu(len), llu(offset), dinbuf);

	assert(osd && osd->root && osd->dbc && dinbuf && sense);

	pairs = sglist->num_entries;
	assert(pairs != 0);

	osd_info("%s: offset,len pairs %llu", __func__, llu(pairs));

	if (!(pid >= USEROBJECT_PID_LB && oid >= USEROBJECT_OID_LB))
		goto out_cdb_err;

	get_dfile_name(path, osd->root, pid, oid);
	fd = open(path, O_RDWR|O_LARGEFILE); /* fails on non-existent obj */
	if (fd < 0)
		goto out_cdb_err;

	data_offset = 0;

	for (i=0; i<pairs; i++) {
		/* offset into dest */
		offset_val = get_ntohll(&sglist->entries[i].offset);
		length = get_ntohll(&sglist->entries[i].bytes_to_transfer);

		osd_info("%s: Offset: %llu Length: %llu",
			  __func__, llu(offset_val + offset), llu(length));

		osd_info("%s: Position in data buffer: %llu",
			  __func__, llu(data_offset));

		osd_info("%s: ------------------------------", __func__);
		ret = pwrite(fd, dinbuf+data_offset, length, offset_val+offset);
		data_offset += length;
		osd_info("%s: return value is %d", __func__, ret);
		if (ret < 0 || (uint64_t)ret != length)
			goto out_hw_err;
	}

	ret = close(fd);
	if (ret != 0)
		goto out_hw_err;

	fill_ccap(&osd->ccap, NULL, USEROBJECT, pid, oid, 0);
	return OSD_OK; /* success */

out_hw_err:
	ret = sense_build_sdd(sense, OSD_SSK_HARDWARE_ERROR,
			      OSD_ASC_INVALID_FIELD_IN_CDB, pid, oid);
	return ret;

out_cdb_err:
	ret = sense_build_sdd(sense, OSD_SSK_ILLEGAL_REQUEST,
			      OSD_ASC_INVALID_FIELD_IN_CDB, pid, oid);
	return ret;


}

static int vec_write(struct osd_device *osd, uint64_t pid, uint64_t oid,
		     uint64_t len, uint64_t offset, const uint8_t *dinbuf,
		     uint8_t *sense)
{
	int ret;
	int fd;
	char path[MAXNAMELEN];
	uint64_t data_offset, offset_val, hdr_offset, length, stride, bytes;
	unsigned int i;

	osd_debug("%s: pid %llu oid %llu len %llu offset %llu data %p",
		  __func__, llu(pid), llu(oid), llu(len), llu(offset), dinbuf);

	assert(osd && osd->root && osd->dbc && dinbuf && sense);

	stride = get_ntohll(dinbuf);
	hdr_offset = sizeof(uint64_t);
	length = get_ntohll(dinbuf + hdr_offset);

	osd_debug("%s: stride is %llu and len is %llu", __func__, llu(stride), llu(length));

	if (!(pid >= USEROBJECT_PID_LB && oid >= USEROBJECT_OID_LB))
		goto out_cdb_err;

	get_dfile_name(path, osd->root, pid, oid);
	fd = open(path, O_RDWR|O_LARGEFILE); /* fails on non-existent obj */
	if (fd < 0)
		goto out_cdb_err;

	data_offset = hdr_offset + sizeof(uint64_t);

	bytes = len - (2*sizeof(uint64_t));

	osd_debug("%s: bytes to write is %llu", __func__, llu(bytes));
	offset_val = 0;
	while (bytes > 0) {
		osd_debug("%s: Position in data buffer: %llu", __func__,
			   llu(data_offset));
		osd_debug("%s: Offset: %llu", __func__, llu(offset_val + offset));
		osd_debug("%s: ------------------------------", __func__);
		ret = pwrite(fd, dinbuf+data_offset, length, offset_val+offset);
		if (ret < 0 || (uint64_t)ret != length)
			goto out_hw_err;
		data_offset += length;
		offset_val += stride;
		bytes -= length;
		if (bytes < length)
			length = bytes;
		osd_debug("%s: Total Bytes Left to write: %llu", __func__,
			  llu(bytes));
	}
	ret = close(fd);
	if (ret != 0)
		goto out_hw_err;

	fill_ccap(&osd->ccap, NULL, USEROBJECT, pid, oid, 0);
	return OSD_OK; /* success */

out_hw_err:
	ret = sense_build_sdd(sense, OSD_SSK_HARDWARE_ERROR,
			      OSD_ASC_INVALID_FIELD_IN_CDB, pid, oid);
	return ret;

out_cdb_err:
	ret = sense_build_sdd(sense, OSD_SSK_ILLEGAL_REQUEST,
			      OSD_ASC_INVALID_FIELD_IN_CDB, pid, oid);
	return ret;


}


int osd_write(struct osd_device *osd, uint64_t pid, uint64_t oid, 
	      uint64_t len, uint64_t offset, const uint8_t *dinbuf, 
	      const struct sg_list *sglist, uint8_t *sense, uint8_t ddt)
{

	/*figure out what kind of write it is based on ddt and call appropriate
	write function*/

	switch(ddt) {
		case DDT_CONTIG: {
			return contig_write(osd, pid, oid, len, offset, dinbuf,
				           sense);
		}
		case DDT_SGL: {
			return sgl_write(osd, pid, oid, len, offset, dinbuf,
					 sglist, sense);
		}
		case DDT_VEC: {
			return vec_write(osd, pid, oid, len, offset, dinbuf,
				           sense);
		}
		default: {
			osd_info("osd_write!!! ddt=%d\n", ddt);

			return sense_build_sdd(sense, OSD_SSK_ILLEGAL_REQUEST,
			               OSD_ASC_INVALID_FIELD_IN_CDB, pid, oid);
		}
	}
}

/*
 * OSD CAS: Available only for USEROBJECTs.
 * TODO: No explicit software locks used. If multithreaded OSD emulator is
 * used behavior is undefined.
 *
 */
int osd_cas(struct osd_device *osd, uint64_t pid, uint64_t oid, uint64_t cmp,
	    uint64_t swap, uint8_t *doutbuf, uint64_t *used_outlen,
	    uint8_t *sense)
{
	int ret;
	int present;
	uint8_t obj_type;
	uint64_t val;
	uint32_t usedlen;

	assert(osd && osd->dbc && doutbuf && sense);

	ret = obj_ispresent(osd->dbc, pid, oid, &present);
	if (ret != OSD_OK || !present) /* object not present! */
		goto out_cdb_err;

	obj_type = get_obj_type(osd, pid, oid);
	if (obj_type != USEROBJECT)
		goto out_cdb_err;

	ret = attr_get_val(osd->dbc, pid, oid, USER_ATOMICS_PG, UAP_CAS,
			   sizeof(val), &val, &usedlen);
	if (ret != -ENOENT && ret != OSD_OK)
		goto out_hw_err;
	/* 
	 * XXX:SD we do a lazy initialization of atomics page. Hence attr
	 * might not be defined before. In that case we define it and proceed
	 * with rest of the operation
	 */
	if (ret == -ENOENT) {
		val = 0;
		ret = attr_set_attr(osd->dbc, pid, oid, USER_ATOMICS_PG, 
				    UAP_CAS, &val, sizeof(val));
		if (ret != OSD_OK)
			goto out_hw_err;
	}

	osd_debug("pid %llu oid %llu cmp %llu swap %llu val %llu", llu(pid),
		  llu(oid), llu(cmp), llu(swap), llu(val));

	if (val == cmp) {
		ret = attr_set_attr(osd->dbc, pid, oid, USER_ATOMICS_PG,
				    UAP_CAS, &swap, sizeof(swap));
		if (ret != OSD_OK)
			goto out_hw_err;
	}

	set_htonll(doutbuf, val);
	*used_outlen = sizeof(val);
	return OSD_OK;

out_hw_err:
	ret = sense_build_sdd(sense, OSD_SSK_HARDWARE_ERROR,
			      OSD_ASC_INVALID_FIELD_IN_CDB, pid, oid);
	return ret;

out_cdb_err:
	ret = sense_build_sdd(sense, OSD_SSK_ILLEGAL_REQUEST,
			      OSD_ASC_INVALID_FIELD_IN_CDB, pid, oid);
	return ret;
}


/*
 * OSD FA: Available only for USEROBJECTs.
 * TODO: No explicit software locks used. If multithreaded OSD emulator is
 * used, behavior is undefined.
 *
 */
int osd_fa(struct osd_device *osd, uint64_t pid, uint64_t oid, int64_t add,
	   uint8_t *doutbuf, uint64_t *used_outlen, uint8_t *sense)
{
	int ret;
	int present;
	uint8_t obj_type;
	uint64_t val;
	uint32_t usedlen;

	assert(osd && osd->dbc && doutbuf && sense);

	ret = obj_ispresent(osd->dbc, pid, oid, &present);
	if (ret != OSD_OK || !present) /* object not present! */
		goto out_cdb_err;

	obj_type = get_obj_type(osd, pid, oid);
	if (obj_type != USEROBJECT)
		goto out_cdb_err;

	ret = attr_get_val(osd->dbc, pid, oid, USER_ATOMICS_PG, UAP_FA,
			   sizeof(val), &val, &usedlen);
	if (ret != -ENOENT && ret != OSD_OK)
		goto out_hw_err;
	/* 
	 * XXX:SD we do a lazy initialization of atomics page. Hence attr
	 * might not be defined before. In that case we define it and proceed
	 * with rest of the operation
	 */
	if (ret == -ENOENT) {
		val = 0;
		ret = attr_set_attr(osd->dbc, pid, oid, USER_ATOMICS_PG, 
				    UAP_FA, &val, sizeof(val));
		if (ret != OSD_OK)
			goto out_hw_err;
	}

	add += val;
	ret = attr_set_attr(osd->dbc, pid, oid, USER_ATOMICS_PG, UAP_FA,
			    &add, sizeof(add));
	if (ret != OSD_OK)
		goto out_hw_err;

	set_htonll(doutbuf, val);
	*used_outlen = sizeof(val);
	return OSD_OK;

out_hw_err:
	ret = sense_build_sdd(sense, OSD_SSK_HARDWARE_ERROR,
			      OSD_ASC_INVALID_FIELD_IN_CDB, pid, oid);
	return ret;

out_cdb_err:
	ret = sense_build_sdd(sense, OSD_SSK_ILLEGAL_REQUEST,
			      OSD_ASC_INVALID_FIELD_IN_CDB, pid, oid);
	return ret;
}

/*
 * OSD_GEN_CAS: generalized cas
 *
 * max(cmp_len and swap_len) == ATTR_LEN_UB == 0xFFFE
 *
 */
int osd_gen_cas(struct osd_device *osd, uint64_t pid, uint64_t oid,
		uint32_t page, uint32_t number, const uint8_t *cmp,
		uint16_t cmp_len, const uint8_t *swap, uint16_t swap_len,
		uint8_t **orig_val, uint16_t *orig_len, uint8_t *sense)
{
	int ret;
	int present;
	uint8_t obj_type;
	uint8_t *val;
	uint32_t valen;

	assert(osd && osd->dbc && orig_val && orig_len && sense);

	val = malloc(ATTR_LEN_UB);
	if (!val)
		goto out_hw_err;

	ret = obj_ispresent(osd->dbc, pid, oid, &present);
	if (ret != OSD_OK || !present) /* object not present */
		goto out_cdb_err;

	obj_type = get_obj_type(osd, pid, oid);
	if (obj_type != USEROBJECT)
		goto out_cdb_err;

	ret = attr_get_val(osd->dbc, pid, oid, page, number, ATTR_LEN_UB,
			   val, &valen);
	if (ret != OSD_OK && ret != -ENOENT)
		goto out_hw_err;

	/*
	 * swap_len determines if the new entry is being inserted or original
	 * entry is to be removed.
	 */
	if ((swap_len > 0 && swap != NULL) &&
	    (ret == -ENOENT ||
	     (valen == cmp_len && memcmp(cmp, val, valen) == 0))) {
		ret = attr_set_attr(osd->dbc, pid, oid, page, number, swap,
				    swap_len);
		if (ret != OSD_OK)
			goto out_hw_err;
	} else if ((swap_len == 0 && swap == NULL) &&
		   (valen == cmp_len && memcmp(cmp, val, valen) == 0)) {
		ret = attr_delete_attr(osd->dbc, pid, oid, page, number);
		if (ret != OSD_OK)
			goto out_hw_err;
	}

	/*
	 * regardless of successful swap, we return original value, if
	 * present, else undefined value
	 */
	if (ret == -ENOENT) {
		*orig_val = NULL;
		*orig_len = 0;
	} else {
		*orig_val = val;
		*orig_len = valen;
	}
	return OSD_OK;

out_hw_err:
	ret = sense_build_sdd(sense, OSD_SSK_HARDWARE_ERROR,
			      OSD_ASC_INVALID_FIELD_IN_CDB, pid, oid);
	return ret;

out_cdb_err:
	ret = sense_build_sdd(sense, OSD_SSK_ILLEGAL_REQUEST,
			      OSD_ASC_INVALID_FIELD_IN_CDB, pid, oid);
	return ret;
}
