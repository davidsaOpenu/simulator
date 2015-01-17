/*
 * Basic DB tests.
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
#include <errno.h>
#include <string.h>
#include <assert.h>

#include "osd.h"
#include "db.h"
#include "attr.h"
#include "obj.h"
#include "coll.h"
#include "osd-util/osd-util.h"

static void test_obj(struct osd_device *osd)
{
	int ret = 0;

	ret = obj_insert(osd->dbc, 1, 2, USEROBJECT);
	assert(ret == 0);

	ret = obj_delete(osd->dbc, 1, 2);
	assert(ret == 0);
}

static void test_dup_obj(struct osd_device *osd)
{
	int ret = 0;

	ret = obj_insert(osd->dbc, 1, 2, USEROBJECT);
	assert(ret == 0);

	/* duplicate insert must fail */
	ret = obj_insert(osd->dbc, 1, 2, USEROBJECT);
	assert(ret != 0);

	ret = obj_delete(osd->dbc, 1, 2);
	assert(ret == 0);
}

static void test_attr(struct osd_device *osd)
{
	int ret= 0;
	const char *attr = "This is first attr";
	uint32_t len = 0;
	uint8_t listfmt = 0;

	ret = attr_set_attr(osd->dbc, 1, 1, 2, 12, attr, strlen(attr)+1);
	assert(ret == 0);

	void *val = Calloc(1, 1024);
	if (!val)
		osd_error_errno("%s: Calloc failed", __func__);

	listfmt = RTRVD_SET_ATTR_LIST;
	ret = attr_get_attr(osd->dbc, 1, 1, 2, 12, 1024, val, listfmt, &len);
	assert(ret == 0);
	uint32_t l = strlen(attr)+1+LE_VAL_OFF;
	l += (0x8 - (l & 0x7)) & 0x7;
	assert(len == l);
#ifndef NDEBUG
	/* ifdef to avoid unused warning */
	struct list_entry *ent = (struct list_entry *)val;
	assert(get_ntohl(&ent->page) == 2);
	assert(get_ntohl(&ent->number) == 12);
	assert(get_ntohs(&ent->len) == strlen(attr)+1);
	assert(strcmp((char *)ent + LE_VAL_OFF, attr) == 0); 
#endif

	/* get non-existing attr, must fail */
	ret = attr_get_attr(osd->dbc, 2, 1, 2, 12, 1024, val, listfmt, &len);
	assert(ret != 0);

	free(val);
}

static void test_obj_manip(struct osd_device *osd)
{
	int i = 0;
	int ret = 0;
	uint64_t oid = 0;
	int present = 0;

	for (i =0; i < 4; i++) {
		ret = obj_insert(osd->dbc, 1, 1<<i, USEROBJECT);
		assert(ret == 0);
	}

	ret = obj_get_nextoid(osd->dbc, 1, &oid);
	assert(ret == 0);
	/* nextoid for partition 1 is 9 */
	assert(oid == 9);

	/* get nextoid for new (pid, oid) */
	ret = obj_get_nextoid(osd->dbc, 4, &oid);
	assert(ret == 0);
	/* nextoid for new partition == 1 */
	assert(oid == 1);

	for (i =0; i < 4; i++) {
		ret = obj_delete(osd->dbc, 1, 1<<i);
		assert(ret == 0);
	}

	ret = obj_insert(osd->dbc, 1, 235, USEROBJECT);
	assert(ret == 0);

	/* existing object, ret == 1 */
	ret = obj_ispresent(osd->dbc, 1, 235, &present);
	assert(ret == 0 && present == 1);

	ret = obj_delete(osd->dbc, 1, 235);
	assert(ret == 0);

	/* non-existing object, ret == 0 */
	ret = obj_ispresent(osd->dbc, 1, 235, &present);
	assert(ret == 0 && present == 0);
}

static void test_pid_isempty(struct osd_device *osd)
{
	int ret = 0;
	int isempty = 0;

	ret = obj_insert(osd->dbc, 1, 1, USEROBJECT);
	assert(ret == 0);

	/* pid is not empty, ret should be 0 */
	ret = obj_isempty_pid(osd->dbc, 1, &isempty);
	assert(ret == 0 && isempty == 0);

	ret = obj_delete(osd->dbc, 1, 1);
	assert(ret == 0);

	/* pid is empty, ret should be 1 */
	ret = obj_isempty_pid(osd->dbc, 1, &isempty);
	assert(ret == 0 && isempty == 1);
}

static void test_get_obj_type(struct osd_device *osd)
{
	int ret = 0;
	uint8_t obj_type = ILLEGAL_OBJ;

	ret = obj_insert(osd->dbc, 1, 1, USEROBJECT);
	assert(ret == 0);

	ret = obj_insert(osd->dbc, 2, 2, COLLECTION);
	assert(ret == 0);

	ret = obj_insert(osd->dbc, 3, 0, PARTITION);
	assert(ret == 0);

	ret = obj_get_type(osd->dbc, 0, 0, &obj_type);
	assert(ret == 0 && obj_type == ROOT);

	ret = obj_get_type(osd->dbc, 1, 1, &obj_type);
	assert(ret == 0 && obj_type == USEROBJECT);

	ret = obj_get_type(osd->dbc, 2, 2, &obj_type);
	assert(ret == 0 && obj_type == COLLECTION);

	ret = obj_get_type(osd->dbc, 3, 0, &obj_type);
	assert(ret == 0 && obj_type == PARTITION);

	ret = obj_delete(osd->dbc, 3, 0);
	assert(ret == 0);

	ret = obj_delete(osd->dbc, 2, 2);
	assert(ret == 0);

	ret = obj_delete(osd->dbc, 1, 1);
	assert(ret == 0);

	/* non-existing object's type must be ILLEGAL_OBJ */
	ret = obj_get_type(osd->dbc, 1, 1, &obj_type);
	assert(ret == 0 && obj_type == ILLEGAL_OBJ);
}

static inline void delete_obj(struct osd_device *osd, uint64_t pid, 
			      uint64_t oid)
{
	int ret = 0;
	ret = obj_delete(osd->dbc, pid, oid);
	assert (ret == 0);
	ret = attr_delete_all(osd->dbc, pid, oid);
	assert (ret == 0);
}

static void test_dir_page(struct osd_device *osd)
{
	int ret= 0;
	uint8_t buf[1024];
	char pg3[] = "OSC     page 3 id                      ";
	char pg4[] = "OSC     page 4 id                      ";
#ifndef NDEBUG
	char uid[] = "        unidentified attributes page   ";
#endif
	uint32_t used_len;
	uint32_t val;

	delete_obj(osd, 1, 1);

	ret = obj_insert(osd->dbc, 1, 1, USEROBJECT);
	assert(ret == 0);

	val = 44;
	ret = attr_set_attr(osd->dbc, 1, 1, 1, 2, &val, sizeof(val));
	assert(ret == 0);
	val = 444;
	ret = attr_set_attr(osd->dbc, 1, 1, 1, 3, &val, sizeof(val));
	assert(ret == 0);
	val = 333;
	ret = attr_set_attr(osd->dbc, 1, 1, 2, 22, &val, sizeof(val));
	assert(ret == 0);
	ret = attr_set_attr(osd->dbc, 1, 1, 3, 0, pg3, sizeof(pg3));
	assert(ret == 0);
	val = 321;
	ret = attr_set_attr(osd->dbc, 1, 1, 3, 3, &val, sizeof(val));
	assert(ret == 0);
	ret = attr_set_attr(osd->dbc, 1, 1, 4, 0, pg4, sizeof(pg4));
	assert(ret == 0);

	memset(buf, 0, sizeof(buf));
	ret = attr_get_dir_page(osd->dbc, 1, 1, USEROBJECT_DIR_PG,
				sizeof(buf), buf, RTRVD_SET_ATTR_LIST,
				&used_len);
	assert (ret == 0);

	uint8_t *cp = buf;
	int i = 0;
	for (i = 1; i <= 4; i++) {
		uint8_t pad = 0;
		uint16_t len = 0;
		uint32_t j = 0;
		assert(get_ntohl(&cp[LE_PAGE_OFF]) == USEROBJECT_DIR_PG);
		j = get_ntohl(&cp[LE_NUMBER_OFF]);
		assert(j == (uint32_t)i);
		len = get_ntohs(&cp[LE_LEN_OFF]);
		assert(len == 40);
		cp += 10;
		if (i == 1 || i == 2)
			assert(strcmp((char *)cp, uid) == 0);
		else if (i == 3)
			assert(strcmp((char *)cp, pg3) == 0);
		else if (i == 4)
			assert(strcmp((char *)cp, pg4) == 0);
		cp += len;
		pad = (0x8 - ((10+len) & 0x7)) & 0x7;
		while (pad--)
			assert(*cp == 0), cp++;
	}

	delete_obj(osd, 1, 1);
}

static void test_coll(struct osd_device *osd)
{
	int ret = 0;
	uint64_t usedlen;
	uint64_t addlen;
	int isempty = 0;
	uint64_t oids[64] = {0};
	uint64_t cont_id = 0xFUL;

	ret = coll_insert(osd->dbc, 0x20, 0x2, 0x2222, 2);
	assert(ret == 0);
	ret = coll_insert(osd->dbc, 0x20, 0x1, 0x1111111111111111, 2);
	assert(ret == 0);
	ret = coll_insert(osd->dbc, 0x20, 0x2, 0x3333333333333333, 2);
	assert(ret == 0);
	ret = coll_insert(osd->dbc, 0x20, 0x2, 0x7888888888888888, 2);
	assert(ret == 0);
	ret = coll_insert(osd->dbc, 0x20, 0x2, 0x7AAAAAAAAAAAAAAA, 2);
	assert(ret == 0);
	ret = coll_insert(osd->dbc, 0x20, 0x2, 0xFFFFFFFFFFFFFFFF, 2);
	assert(ret == 0);
	ret = coll_insert(osd->dbc, 0x20, 0x1, 0x111, 2);
	assert(ret == 0);

	ret = coll_get_oids_in_cid(osd->dbc, 0x20, 0x2, 0, 64*8, 
				   (uint8_t *)oids, &usedlen, &addlen,
				   &cont_id); 
	assert(ret == 0);

	/* 
	 * XXX: the following is sqlite bug. sqlite converts uint64_t to
	 * double due to which 0xFFFFFFFFFFFFFFFF is interpreted as -1 and
	 * fails to be selected
	 */
	assert(usedlen == 4*8);
	assert(addlen == 4*8);
	assert(cont_id == 0);
	assert(get_ntohll(&oids[0]) == 0x2222);
	assert(get_ntohll(&oids[1]) == 0x3333333333333333);
	assert(get_ntohll(&oids[2]) == 0x7888888888888888);
	assert(get_ntohll(&oids[3]) == 0x7AAAAAAAAAAAAAAA);

	/* 
	 * XXX: following is sqlite bug. sqlite converts uint64_t to double
	 * and looses precision 
	 */
	assert(get_ntohll(&oids[4]) != 0xFFFFFFFFFFFFFFFF); 

	/* test empty cid */
	ret = coll_isempty_cid(osd->dbc, 0x20, 0x1, &isempty);
	assert(ret == 0 && isempty == 0);
	ret = coll_isempty_cid(osd->dbc, 0x20, 0x2, &isempty);
	assert(ret == 0 && isempty == 0);
	ret = coll_isempty_cid(osd->dbc, 0x20, 0x3, &isempty);
	assert(ret == 0 && isempty == 1);

	/* 
	 * TODO: fill in 16k rows in coll_tab and empty it to see if vaccum
	 * affects prepared statements
	 */

	ret = coll_delete_cid(osd->dbc, 0x20, 0x1);
	assert(ret == 0);
	ret = coll_delete_cid(osd->dbc, 0x20, 0x2);
	assert(ret == 0);
	ret = coll_isempty_cid(osd->dbc, 0x20, 0x1, &isempty);
	assert(ret == 0 && isempty == 1);
	ret = coll_isempty_cid(osd->dbc, 0x20, 0x2, &isempty);
	assert(ret == 0 && isempty == 1);
}


static void test_copy_coll(struct osd_device *osd)
{
	int ret = 0;
	uint64_t usedlen;
	uint64_t addlen;
	uint64_t oids[64] = {0};
	uint64_t cont_id = 0xFUL;

	ret = coll_insert(osd->dbc, 0x20, 0x2, 0x2222, 2);
	assert(ret == 0);
	ret = coll_insert(osd->dbc, 0x20, 0x1, 0x1111111111111111, 2);
	assert(ret == 0);
	ret = coll_insert(osd->dbc, 0x20, 0x2, 0x3333333333333333, 2);
	assert(ret == 0);
	ret = coll_insert(osd->dbc, 0x20, 0x2, 0x7888888888888888, 2);
	assert(ret == 0);
	ret = coll_insert(osd->dbc, 0x20, 0x2, 0x7AAAAAAAAAAAAAAA, 2);
	assert(ret == 0);
	ret = coll_insert(osd->dbc, 0x20, 0x2, 0xFFFFFFFFFFFFFFFF, 2);
	assert(ret == 0);
	ret = coll_insert(osd->dbc, 0x20, 0x1, 0x111, 2);
	assert(ret == 0);

	/* copy collection */
	ret = coll_copyoids(osd->dbc, 0x20, 0x3, 0x1);
	assert(ret == 0);

	/* list elements in dest collection */
	ret = coll_get_oids_in_cid(osd->dbc, 0x20, 0x3, 0, 64*8, 
				   (uint8_t *)oids, &usedlen, &addlen,
				   &cont_id); 
	assert(ret == 0);
	assert(usedlen == 2*8);
	assert(addlen == 2*8);
	assert(cont_id == 0);
	assert(get_ntohll(&oids[0]) == 0x111);
	assert(get_ntohll(&oids[1]) == 0x1111111111111111);
}

int main()
{
	int ret = 0;
	struct osd_device osd;
	const char *root = "/tmp/osd/";

	system("rm -rf /tmp/osd/");
	ret = osd_open(root, &osd);
	assert(ret == 0);

 	ret = db_exec_pragma(osd.dbc);
	assert(ret == 0); 

      	test_obj(&osd);
	test_dup_obj(&osd);
	test_obj_manip(&osd);
	test_pid_isempty(&osd);
	test_get_obj_type(&osd);
	test_attr(&osd);  
 	test_dir_page(&osd); 
	test_coll(&osd); 
	test_copy_coll(&osd);

	ret = db_print_pragma(osd.dbc);
	assert(ret == 0);

	ret = osd_close(&osd);
	assert(ret == 0);

	return 0;
}
