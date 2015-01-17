/*
 * Basic CDB tests.
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
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

#include "osd-types.h"
#include "osd.h"
#include "cdb.h"
#include "osd-util/osd-util.h"
#include "osd-util/osd-sense.h"
#include "osd-initiator/command.h"

void test_partition(struct osd_device *osd);
void test_create(struct osd_device *osd);
void test_query(struct osd_device *osd);
void test_list(struct osd_device *osd);
void test_set_member_attributes(struct osd_device *osd);
void test_atomics(struct osd_device *osd);

void test_partition(struct osd_device *osd) 
{
	int ret = 0;
	struct osd_command cmd;
	int senselen_out;
	uint8_t sense_out[OSD_MAX_SENSE];
	uint8_t *data_out = NULL;
	const void *data_in;
	uint64_t data_out_len, data_in_len;

	/* create partition + empty getpage_setlist */
	ret = osd_command_set_create_partition(&cmd, PARTITION_PID_LB);
	assert(ret == 0);
	ret = osdemu_cmd_submit(osd, cmd.cdb, NULL, 0, &data_out,
				&data_out_len, sense_out, &senselen_out);
	assert(ret == 0);

	/* remove partition + empty getpage_setlist */
	memset(&cmd, 0, sizeof(cmd));
	ret = osd_command_set_remove_partition(&cmd, PARTITION_PID_LB);
	assert(ret == 0);
	ret = osdemu_cmd_submit(osd, cmd.cdb, NULL, 0, &data_out,
				&data_out_len, sense_out, &senselen_out);
	assert(ret == 0);

	/* create partition + empty getlist_setlist */
	struct attribute_list attr = {ATTR_GET_PAGE, 0, 0, NULL, 0, 0};

	ret = osd_command_set_create_partition(&cmd, PARTITION_PID_LB);
	assert(ret == 0);
	ret = osd_command_attr_build(&cmd, &attr, 1);
	assert (ret == 0);
	data_in = cmd.outdata;
	data_in_len = cmd.outlen;
	ret = osdemu_cmd_submit(osd, cmd.cdb, cmd.outdata, cmd.outlen,
				&data_out, &data_out_len, sense_out,
				&senselen_out);
	assert(ret == 0);

	/* remove partition + empty getpage_setlist */
	ret = osd_command_set_remove_partition(&cmd, PARTITION_PID_LB);
	assert(ret == 0);
	ret = osd_command_attr_build(&cmd, &attr, 1);
	assert (ret == 0);
	data_in = cmd.outdata;
	data_in_len = cmd.outlen;
	ret = osdemu_cmd_submit(osd, cmd.cdb, cmd.outdata, cmd.outlen,
				&data_out, &data_out_len, sense_out,
				&senselen_out);
	assert(ret == 0);

	free(cmd.attr_malloc);

}

void test_create(struct osd_device *osd)
{
	int ret = 0;
	struct osd_command cmd;
	int senselen_out;
	uint8_t sense_out[OSD_MAX_SENSE];
	uint8_t *data_out = NULL;
	const void *data_in;
	uint64_t data_out_len, data_in_len;
	uint8_t *cp = NULL;
	uint8_t pad = 0;
	uint32_t len = 0;

	/* create partition + empty getpage_setlist */
	ret = osd_command_set_create_partition(&cmd, PARTITION_PID_LB);
	assert(ret == 0);
	ret = osdemu_cmd_submit(osd, cmd.cdb, NULL, 0, &data_out,
				&data_out_len, sense_out, &senselen_out);
	assert(ret == 0);

	/* create 1 object */
	ret = osd_command_set_create(&cmd, USEROBJECT_PID_LB,
				     USEROBJECT_OID_LB, 1); 
	assert(ret == 0);
	ret = osdemu_cmd_submit(osd, cmd.cdb, NULL, 0, &data_out, &data_out_len,
				sense_out, &senselen_out);
	assert(ret == 0);

	/* remove the object */
	ret = osd_command_set_remove(&cmd, USEROBJECT_PID_LB, 
				     USEROBJECT_OID_LB);
	assert(ret == 0);
	ret = osdemu_cmd_submit(osd, cmd.cdb, NULL, 0, &data_out, &data_out_len,
				sense_out, &senselen_out);
	assert(ret == 0);

	struct attribute_list attr = {
		ATTR_GET_PAGE, CUR_CMD_ATTR_PG, 0, NULL, CCAP_TOTAL_LEN, 0
	};
	/* create 5 objects & get ccap */
	ret = osd_command_set_create(&cmd, USEROBJECT_PID_LB, 0, 5);
	assert(ret == 0);
	ret = osd_command_attr_build(&cmd, &attr, 1);
	assert(ret == 0);
	ret = osdemu_cmd_submit(osd, cmd.cdb, NULL, 0, &data_out, 
				&data_out_len, sense_out, &senselen_out);
	assert(ret == 0);

	assert(get_ntohl(&data_out[0]) == CUR_CMD_ATTR_PG);
	assert(get_ntohl(&data_out[4]) == CCAP_TOTAL_LEN - 8);
	assert(data_out[CCAP_OBJT_OFF] == USEROBJECT);
	assert(get_ntohll(&data_out[CCAP_PID_OFF]) == USEROBJECT_PID_LB);
	assert(get_ntohll(&data_out[CCAP_APPADDR_OFF]) == 0);
	uint64_t i = get_ntohll(&data_out[CCAP_OID_OFF]);
	assert (i == (USEROBJECT_PID_LB + 5 - 1));

	free(data_out);
	data_out = NULL;
	data_out_len = 0;

	i -= (5-1);
	/* remove 5 objects */
	for (;i < USEROBJECT_OID_LB + 5; i++) {
		ret = osd_command_set_remove(&cmd, USEROBJECT_PID_LB, i);
		assert(ret == 0);
		ret = osdemu_cmd_submit(osd, cmd.cdb, NULL, 0, &data_out,
					&data_out_len, sense_out,
					&senselen_out);
		assert(ret == 0);
	}

	/* create 5 objects, set 2 attr on each */
	char str1[MAXNAMELEN], str2[MAXNAMELEN];
	sprintf(str1, "Madhuri Dixit Rocks!!");
	sprintf(str2, "A ciggarate a day, kills a moron anyway.");
	struct attribute_list setattr[] = {
		{ATTR_SET, USEROBJECT_PG+LUN_PG_LB, 111, str1, strlen(str1)+1, 
			0
		}, 
		{ATTR_SET, USEROBJECT_PG+LUN_PG_LB+1, 321, str2,
			strlen(str2)+1, 0
		} 
	};

	ret = osd_command_set_create(&cmd, USEROBJECT_PID_LB, 0, 5);
	assert(ret == 0);
	ret = osd_command_attr_build(&cmd, setattr, 2);
	assert(ret == 0);
	data_in = cmd.outdata;
	data_in_len = cmd.outlen;
	ret = osdemu_cmd_submit(osd, cmd.cdb, data_in, data_in_len, &data_out,
				&data_out_len, sense_out, &senselen_out);
	assert(ret == 0);

	/* remove 5 objects and get previously set attributes for each */
	struct attribute_list getattr[] = { 
		{ATTR_GET, USEROBJECT_PG+LUN_PG_LB+1, 321, NULL, strlen(str2), 
			0
		}, 
		{ATTR_GET, USEROBJECT_PG+LUN_PG_LB, 111, NULL, strlen(str1),
			0
		} 
	};
	for (i = USEROBJECT_OID_LB; i < (USEROBJECT_OID_LB + 5); i++) {
		ret = osd_command_set_remove(&cmd, USEROBJECT_PID_LB, i);
		assert(ret == 0);
		ret = osd_command_attr_build(&cmd, getattr, 2);
		assert(ret == 0);
		data_in = cmd.outdata;
		data_in_len = cmd.outlen;
		ret = osdemu_cmd_submit(osd, cmd.cdb, data_in, data_in_len,
					&data_out, &data_out_len, sense_out,
					&senselen_out);
		assert(ret == 0);
		assert(data_out[0] == RTRVD_SET_ATTR_LIST);
		len = get_ntohl(&data_out[4]);
		assert(len > 0);
		cp = &data_out[8];
		assert(get_ntohl(&cp[LE_PAGE_OFF]) == USEROBJECT_PG+LUN_PG_LB+1);
		assert(get_ntohl(&cp[LE_NUMBER_OFF]) == 321);
		len = get_ntohs(&cp[LE_LEN_OFF]);
		assert((uint32_t)len == (strlen(str2)+1));
		assert(memcmp(&cp[LE_VAL_OFF], str2, len) == 0);
		cp += len + LE_VAL_OFF;
		pad = (0x8 - ((uintptr_t)cp & 0x7)) & 0x7;
		while (pad--)
			assert(*cp == 0), cp++;
		assert(get_ntohl(&cp[LE_PAGE_OFF]) == USEROBJECT_PG+LUN_PG_LB);
		assert(get_ntohl(&cp[LE_NUMBER_OFF]) == 111);
		len = get_ntohs(&cp[LE_LEN_OFF]);
		assert((uint32_t)len == (strlen(str1)+1));
		assert(memcmp(&cp[LE_VAL_OFF], str1, len) == 0);
		cp += len + LE_VAL_OFF;
		pad = (0x8 - ((uintptr_t)cp & 0x7)) & 0x7;
		while (pad--)
			assert(*cp == 0), cp++;
	}

	free(data_out);
	data_out = NULL;
	data_out_len = 0;

	/* get all attributes in a page for an object */
	ret = osd_command_set_create(&cmd, USEROBJECT_PID_LB, 
				     USEROBJECT_OID_LB, 1);
	assert(ret == 0);
	setattr[0].page = USEROBJECT_PG+LUN_PG_LB+11;
	setattr[1].page = USEROBJECT_PG+LUN_PG_LB+11;
	ret = osd_command_attr_build(&cmd, setattr, 2);
	assert(ret == 0);
	data_in = cmd.outdata;
	data_in_len = cmd.outlen;
	ret = osdemu_cmd_submit(osd, cmd.cdb, data_in, data_in_len, &data_out,
				&data_out_len, sense_out, &senselen_out);
	assert(ret == 0);

	struct attribute_list getallattr[] = {
		{ ATTR_GET, USEROBJECT_PG+LUN_PG_LB+11, ATTRNUM_GETALL, NULL, 
			1024, 0,
		},
	};
	ret = osd_command_set_get_attributes(&cmd, USEROBJECT_PID_LB, 
					     USEROBJECT_OID_LB);
	assert(ret == 0);
	ret = osd_command_attr_build(&cmd, getallattr, 1);
	assert(ret == 0);
	data_in = cmd.outdata;
	data_in_len = cmd.outlen;
	ret = osdemu_cmd_submit(osd, cmd.cdb, data_in, data_in_len, &data_out,
				&data_out_len, sense_out, &senselen_out);
	assert(ret == 0);

	assert(data_out[0] == RTRVD_SET_ATTR_LIST);
	len = get_ntohl(&data_out[4]);
	assert(len > 0);
	cp = &data_out[8];
	assert(get_ntohl(&cp[LE_PAGE_OFF]) == USEROBJECT_PG+LUN_PG_LB+11);
	assert(get_ntohl(&cp[LE_NUMBER_OFF]) == 111);
	len = get_ntohs(&cp[LE_LEN_OFF]);
	assert((uint32_t)len == (strlen(str1)+1));
	assert(memcmp(&cp[LE_VAL_OFF], str1, len) == 0);
	cp += len + LE_VAL_OFF;
	pad = (0x8 - ((uintptr_t)cp & 0x7)) & 0x7;
	while (pad--)
		assert(*cp == 0), cp++;
	assert(get_ntohl(&cp[LE_PAGE_OFF]) == USEROBJECT_PG+LUN_PG_LB+11);
	assert(get_ntohl(&cp[LE_NUMBER_OFF]) == 321);
	len = get_ntohs(&cp[LE_LEN_OFF]);
	assert((uint32_t)len == (strlen(str2)+1));
	assert(memcmp(&cp[LE_VAL_OFF], str2, len) == 0);
	cp += len + LE_VAL_OFF;
	pad = (0x8 - ((uintptr_t)cp & 0x7)) & 0x7;
	while (pad--)
		assert(*cp == 0), cp++;

	free(data_out);
	data_out = NULL;
	data_out_len = 0;

	ret = osd_command_set_remove(&cmd, USEROBJECT_PID_LB, 
				     USEROBJECT_OID_LB);
	assert(ret == 0);
	ret = osdemu_cmd_submit(osd, cmd.cdb, NULL, 0, &data_out,
				&data_out_len, sense_out, &senselen_out);
	assert(ret == 0);

	/* remove partition */
	ret = osd_command_set_remove_partition(&cmd, PARTITION_PID_LB);
	assert(ret == 0);
	ret = osdemu_cmd_submit(osd, cmd.cdb, NULL, 0, &data_out,
				&data_out_len, sense_out, &senselen_out);
	assert(ret == 0);

	free(cmd.attr_malloc);
}


/* only to be used by test_set_one_attr */
static void set_one_attr_int(struct osd_device *osd, uint64_t pid, uint64_t oid, 
			 uint32_t page, uint32_t number, uint64_t val)
{
	struct osd_command cmd;
	uint8_t sense_out[OSD_MAX_SENSE];
	int senselen_out;
	uint8_t *data_out = NULL;
	uint64_t data_out_len;
	uint64_t attrval;
	int ret;
	struct attribute_list attr = {
		.type = ATTR_SET,
		.page = page,
		.number = number,
		.len = 8,
		.val = &attrval,
	};

	set_htonll(&attrval, val);
	ret = osd_command_set_set_attributes(&cmd, pid, oid);
	assert(ret == 0);
	ret = osd_command_attr_build(&cmd, &attr, 1);
	assert(ret == 0);
	ret = osdemu_cmd_submit(osd, cmd.cdb, cmd.outdata, cmd.outlen,
				&data_out, &data_out_len,
				sense_out, &senselen_out);
	assert(ret == 0);
	osd_command_attr_free(&cmd);
}

static void set_one_attr_val(struct osd_device *osd, uint64_t pid, uint64_t oid,
			 uint32_t page, uint32_t number, const void *val,
			 uint16_t len)
{
	struct osd_command cmd;
	uint8_t sense_out[OSD_MAX_SENSE];
	int senselen_out;
	uint8_t *data_out = NULL;
	uint64_t data_out_len;
	int ret,i;
	struct attribute_list attr = {
		.type = ATTR_SET,
		.page = page,
		.number = number,
		.len = len,
		.val = (void *)(uintptr_t) val,
	};

	ret = osd_command_set_set_attributes(&cmd, pid, oid);
	assert(ret == 0);
	ret = osd_command_attr_build(&cmd, &attr, 1);

	
	ret = osdemu_cmd_submit(osd, cmd.cdb, cmd.outdata, cmd.outlen,
				&data_out, &data_out_len,
				sense_out, &senselen_out);
	assert(ret == 0);
	osd_command_attr_free(&cmd);

	/* output cdbfmt , length , value */
	printf("cdbfmt is: %x \n", cmd.cdb[11]);
	printf("length is: %x \n", cmd.cdb[61]);
	printf("value is: ");
 	for(i=0; i<=len; i++){
	        printf("%c", cmd.cdb[62+i]);
	}
	printf("\n");     
}

static void test_set_one_attr (struct osd_device *osd)
{
	struct osd_command cmd;
	uint64_t pid = USEROBJECT_PID_LB;
       	uint64_t oid = USEROBJECT_OID_LB;
	uint8_t *data_out = NULL;
	uint64_t data_out_len;
	uint8_t sense_out[OSD_MAX_SENSE];
	int senselen_out;
	int ret;
    	uint32_t page = USEROBJECT_PG + LUN_PG_LB;
       	
	/* create a partition*/
	ret = osd_command_set_create_partition(&cmd, pid);
	assert(ret == 0);
	ret = osdemu_cmd_submit(osd, cmd.cdb, NULL, 0, &data_out,
				&data_out_len, sense_out, &senselen_out);
	assert(ret == 0);
	
	/* creat one object */
	ret = osd_command_set_create(&cmd, USEROBJECT_PID_LB,
				     USEROBJECT_OID_LB, 1); 
	assert(ret == 0);
	ret = osdemu_cmd_submit(osd, cmd.cdb, NULL, 0, &data_out, &data_out_len,
				sense_out, &senselen_out);
	assert(ret == 0);
		
	/* these cases should not generate error */
	set_one_attr_val(osd, pid, oid, page, 1, "test", 5);
	set_one_attr_val(osd, pid, oid, page, 1, "test_set_one_attr", 18);
	set_one_attr_int(osd, pid, oid, page, 1, 10);
	set_one_attr_int(osd, pid, oid, page, 1, 20);

	/* these cases must generate error */
	/* set_one_attr_val(osd, pid, oid, page, 1, "ttest_set_one_attr", 19); */
	/* set_one_attr_val(osd, pid, oid, page, 1, "", 0); */
}


/* only to be used by test_osd_query */
static void set_attr_int(struct osd_device *osd, uint64_t pid, uint64_t oid, 
			 uint32_t page, uint32_t number, uint64_t val)
{
	struct osd_command cmd;
	uint8_t sense_out[OSD_MAX_SENSE];
	int senselen_out;
	uint8_t *data_out = NULL;
	uint64_t data_out_len;
	uint64_t attrval;
	int ret;
	struct attribute_list attr = {
		.type = ATTR_SET,
		.page = page,
		.number = number,
		.len = 8,
		.val = &attrval,
	};

	set_htonll(&attrval, val);
	ret = osd_command_set_set_attributes(&cmd, pid, oid);
	assert(ret == 0);
	ret = osd_command_attr_build(&cmd, &attr, 1);
	assert(ret == 0);
	ret = osdemu_cmd_submit(osd, cmd.cdb, cmd.outdata, cmd.outlen,
				&data_out, &data_out_len,
				sense_out, &senselen_out);
	assert(ret == 0);
	osd_command_attr_free(&cmd);
}

static void set_attr_val(struct osd_device *osd, uint64_t pid, uint64_t oid,
			 uint32_t page, uint32_t number, const void *val,
			 uint16_t len)
{
	struct osd_command cmd;
	uint8_t sense_out[OSD_MAX_SENSE];
	int senselen_out;
	uint8_t *data_out = NULL;
	uint64_t data_out_len;
	int ret;
	struct attribute_list attr = {
		.type = ATTR_SET,
		.page = page,
		.number = number,
		.len = len,
		.val = (void *)(uintptr_t) val,
	};

	ret = osd_command_set_set_attributes(&cmd, pid, oid);
	assert(ret == 0);
	ret = osd_command_attr_build(&cmd, &attr, 1);
	assert(ret == 0);
	ret = osdemu_cmd_submit(osd, cmd.cdb, cmd.outdata, cmd.outlen,
				&data_out, &data_out_len,
				sense_out, &senselen_out);
	assert(ret == 0);
	osd_command_attr_free(&cmd);
}


static void set_qce(uint8_t *cp, uint32_t page, uint32_t number, 
		    uint16_t min_len, const void *min_val, 
		    uint16_t max_len, const void *max_val)
{
	uint16_t len = 4 + 4 + 2 + min_len + 2 + max_len;

	set_htons(&cp[2], len);
	set_htonl(&cp[4], page);
	set_htonl(&cp[8], number);
	set_htons(&cp[12], min_len);
	memcpy(&cp[14], min_val, min_len);
	set_htons(&cp[14+min_len], max_len);
	memcpy(&cp[16+min_len], max_val, max_len);
}

static int ismember(uint64_t needle, uint64_t *hay, uint64_t haysz)
{

	while (haysz--)
		if (needle == hay[haysz])
			return 1;
	return 0;
}

static void check_results(uint8_t *matches, uint64_t matchlen,
			  uint64_t *idlist, uint64_t idlistlen)
{
	uint32_t add_len = get_ntohll(&matches[0]);

	assert(add_len == (5+8*idlistlen));
	assert(matches[12] == (0x21 << 2));
	assert(matchlen == add_len+8);
	add_len -= 5;
	matches += MIN_ML_LEN;
	while (add_len) {
		assert(ismember(get_ntohll(matches), idlist, 8));
		matches += 8;
		add_len -= 8;
	}
}


void test_query(struct osd_device *osd)
{
	struct osd_command cmd;
	uint64_t pid = PARTITION_PID_LB;
	uint64_t cid = COLLECTION_OID_LB;
	uint64_t oid = USEROBJECT_OID_LB + 1;  /* leave room for cid */
	uint8_t *data_out = NULL;
	uint64_t data_out_len;
	uint8_t sense_out[OSD_MAX_SENSE];
	int senselen_out;
	int i, ret;

	/* create a collection and stick some objects in it */
	ret = osd_command_set_create_partition(&cmd, pid);
	assert(ret == 0);
	ret = osdemu_cmd_submit(osd, cmd.cdb, NULL, 0, &data_out,
				&data_out_len, sense_out, &senselen_out);
	assert(ret == 0);
	ret = osd_command_set_create_collection(&cmd, pid, cid);
	assert(ret == 0);
	ret = osdemu_cmd_submit(osd, cmd.cdb, NULL, 0, &data_out,
				&data_out_len, sense_out, &senselen_out);
	assert(ret == 0);

	/* but don't put all of the objects in the collection */
	for (i=0; i<10; i++) {
		uint64_t attrval;
		struct attribute_list attr = {
			.type = ATTR_SET,
			.page = USER_COLL_PG,
			.number = 1,
			.len = 8,
			.val = &attrval,
		};
		set_htonll(&attrval, cid);
		ret = osd_command_set_create(&cmd, pid, oid + i, 1);
		assert(ret == 0);
		if (!(i == 2 || i == 8)) {
			ret = osd_command_attr_build(&cmd, &attr, 1);
			assert(ret == 0);
		}
		ret = osdemu_cmd_submit(osd, cmd.cdb, cmd.outdata, cmd.outlen,
					&data_out, &data_out_len,
					sense_out, &senselen_out);
		assert(ret == 0);
		osd_command_attr_free(&cmd);
	}

	/*
	 * Set some random attributes for querying.
	 */
	uint32_t page = USEROBJECT_PG + LUN_PG_LB;
	set_attr_int(osd, pid, oid,   page, 1, 4);
	set_attr_int(osd, pid, oid+1, page, 1, 49);
	set_attr_int(osd, pid, oid+1, page, 2, 130);
	set_attr_int(osd, pid, oid+2, page, 1, 20);
	set_attr_int(osd, pid, oid+3, page, 1, 101);
	set_attr_int(osd, pid, oid+4, page, 1, 59);
	set_attr_int(osd, pid, oid+4, page, 2, 37);
	set_attr_int(osd, pid, oid+5, page, 1, 75);
	set_attr_int(osd, pid, oid+6, page, 1, 200);
	set_attr_int(osd, pid, oid+7, page, 1, 67);
	set_attr_int(osd, pid, oid+8, page, 1, 323);
	set_attr_int(osd, pid, oid+8, page, 2, 44);
	set_attr_int(osd, pid, oid+9, page, 1, 1);
	set_attr_int(osd, pid, oid+9, page, 2, 19);

	/*
	 * Various queries.
	 */
	/* run without query criteria */
	uint8_t buf[1024], *cp, *matches;
	uint32_t qll;
	uint64_t matchlen;
	uint64_t idlist[8];

	qll = MINQLISTLEN;
	memset(buf, 0, 1024);
	ret = osd_command_set_query(&cmd, pid, cid, qll, 4096);
	assert(ret == 0);
	ret = osdemu_cmd_submit(osd, cmd.cdb, buf, qll, &matches,
				&matchlen, sense_out, &senselen_out);
	assert(ret == 0);

	idlist[0] = oid;
	idlist[1] = oid+1;
	idlist[2] = oid+3;
	idlist[3] = oid+4;
	idlist[4] = oid+5;
	idlist[5] = oid+6;
	idlist[6] = oid+7;
	idlist[7] = oid+9;
	check_results(matches, matchlen, idlist, 8);

	free(matches);
	matches = NULL;
	matchlen = 0;

	/* run one query without min/max constraints */
	qll = 0;
	memset(buf, 0, 1024);
	cp = buf;
	set_qce(&cp[4], page, 2, 0, NULL, 0, NULL);
	qll += 4 + (4+4+4+2+2);

	ret = osd_command_set_query(&cmd, pid, cid, qll, 4096);
	assert(ret == 0);
	ret = osdemu_cmd_submit(osd, cmd.cdb, buf, qll, &matches,
				&matchlen, sense_out, &senselen_out);
	assert(ret == 0);

	idlist[0] = oid+1; 
	idlist[1] = oid+4; 
	idlist[2] = oid+9;
	check_results(matches, matchlen, idlist, 3);

	free(matches);
	matches = NULL;
	matchlen = 0;

	/* run one query with criteria */
	uint64_t min, max;
	qll = 0;
	min = 40, max= 80;
	set_htonll(&min, min);
	set_htonll(&max, max);
	memset(buf, 0, 1024);
	cp = buf;
	cp[0] = 0x0;
	set_qce(&cp[4], page, 1, sizeof(min), &min, sizeof(max), &max);
	qll += 4 + (4+4+4+2+sizeof(min)+2+sizeof(max));

	ret = osd_command_set_query(&cmd, pid, cid, qll, 4096);
	assert(ret == 0);
	ret = osdemu_cmd_submit(osd, cmd.cdb, buf, qll, &matches,
				&matchlen, sense_out, &senselen_out);
	assert(ret == 0);

	idlist[0] = oid+1; 
	idlist[1] = oid+4; 
	idlist[2] = oid+5;
	idlist[3] = oid+7;
	check_results(matches, matchlen, idlist, 4);

	free(matches);
	matches = NULL;
	matchlen = 0;

	/* run union of two query criteria */
	qll = 0;

	/* first query */
	min = 100, max = 180;
	set_htonll(&min, min);
	set_htonll(&max, max);
	memset(buf, 0, 1024);
	cp = buf;
	cp[0] = 0x0; /* UNION */
	set_qce(&cp[4], page, 1, sizeof(min), &min, sizeof(max), &max);
	qll += 4 + (4+4+4+2+sizeof(min)+2+sizeof(max));
	cp += 4 + (4+4+4+2+sizeof(min)+2+sizeof(max));

	/* second query */
	min = 200, max = 323;
	set_htonll(&min, min);
	set_htonll(&max, max);
	set_qce(cp, page, 1, sizeof(min), &min, sizeof(max), &max);
	qll += (4+4+4+2+sizeof(min)+2+sizeof(max));
	cp += (4+4+4+2+sizeof(min)+2+sizeof(max));

	ret = osd_command_set_query(&cmd, pid, cid, qll, 4096);
	assert(ret == 0);
	ret = osdemu_cmd_submit(osd, cmd.cdb, buf, qll, &matches,
				&matchlen, sense_out, &senselen_out);
	assert(ret == 0);

	idlist[0] = oid+3; 
	idlist[1] = oid+6; 
	check_results(matches, matchlen, idlist, 2);

	free(matches);
	matches = NULL;
	matchlen = 0;

	/* run intersection of 2 query criteria */
	qll = 0;

	/* first query */
	min = 4, max = 100;
	set_htonll(&min, min);
	set_htonll(&max, max);
	memset(buf, 0, 1024);
	cp = buf;
	cp[0] = 0x1; /* INTERSECTION */
	set_qce(&cp[4], page, 1, sizeof(min), &min, sizeof(max), &max);
	qll += 4 + (4+4+4+2+sizeof(min)+2+sizeof(max));
	cp += 4 + (4+4+4+2+sizeof(min)+2+sizeof(max));

	/* second query */
	min = 10, max = 400;
	set_htonll(&min, min);
	set_htonll(&max, max);
	set_qce(cp, page, 2, sizeof(min), &min, sizeof(max), &max);
	qll += (4+4+4+2+sizeof(min)+2+sizeof(max));
	cp += (4+4+4+2+sizeof(min)+2+sizeof(max));

	ret = osd_command_set_query(&cmd, pid, cid, qll, 4096);
	assert(ret == 0);
	ret = osdemu_cmd_submit(osd, cmd.cdb, buf, qll, &matches,
				&matchlen, sense_out, &senselen_out);
	assert(ret == 0);

	idlist[0] = oid+1; 
	idlist[1] = oid+4; 
	check_results(matches, matchlen, idlist, 2);

	free(matches);
	matches = NULL;
	matchlen = 0;

	/* run union of 3 query criteria, with missing min/max */
	qll = 0;

	/* first query */
	min = 130, max = 130;
	set_htonll(&min, min);
	set_htonll(&max, max);
	memset(buf, 0, 1024);
	cp = buf;
	cp[0] = 0x0; /* UNION */
	set_qce(&cp[4], page, 2, sizeof(min), &min, sizeof(max), &max);
	qll += 4 + (4+4+4+2+sizeof(min)+2+sizeof(max));
	cp += 4 + (4+4+4+2+sizeof(min)+2+sizeof(max));

	/* second query */
	min = 150;
	set_htonll(&min, min);
	set_qce(cp, page, 1, sizeof(min), &min, 0, NULL);
	qll += (4+4+4+2+sizeof(min)+2+0);
	cp += (4+4+4+2+sizeof(min)+2+0);

	/* third query */
	max = 10;
	set_htonll(&max, max);
	set_qce(cp, page, 1, 0, NULL, sizeof(max), &max);
	qll += (4+4+4+2+0+2+sizeof(max));
	cp += (4+4+4+2+0+2+sizeof(max));

	ret = osd_command_set_query(&cmd, pid, cid, qll, 4096);
	assert(ret == 0);
	ret = osdemu_cmd_submit(osd, cmd.cdb, buf, qll, &matches,
				&matchlen, sense_out, &senselen_out);
	assert(ret == 0);

	idlist[3] = oid;
	idlist[4] = oid+1;
	idlist[5] = oid+6;
	idlist[2] = oid+9;
	check_results(matches, matchlen, idlist, 4);

	free(matches);
	matches = NULL;
	matchlen = 0;

	/* set some attributes with text values */
	set_attr_val(osd, pid, oid,   page, 1, "hello", 6);
	set_attr_val(osd, pid, oid+1, page, 1, "cat", 4);
	set_attr_int(osd, pid, oid+1, page, 2, 130);
	set_attr_int(osd, pid, oid+2, page, 1, 20);
	set_attr_val(osd, pid, oid+3, page, 1, "zebra", 6);
	set_attr_int(osd, pid, oid+4, page, 1, 59);
	set_attr_int(osd, pid, oid+4, page, 2, 37);
	set_attr_int(osd, pid, oid+5, page, 1, 75);
	set_attr_val(osd, pid, oid+6, page, 1, "keema", 6);
	set_attr_int(osd, pid, oid+7, page, 1, 67);
	set_attr_int(osd, pid, oid+8, page, 1, 323);
	set_attr_int(osd, pid, oid+8, page, 2, 44);
	set_attr_int(osd, pid, oid+9, page, 1, 1);
	set_attr_val(osd, pid, oid+9, page, 2, "hotelling", 10);

	/* run queries on different datatypes, with diff min max lengths */
	qll = 0;

	/* first query */
	min = 41, max = 169;
	set_htonll(&min, min);
	set_htonll(&max, max);
	memset(buf, 0, 1024);
	cp = buf;
	cp[0] = 0x0; /* UNION */
	set_qce(&cp[4], page, 1, sizeof(min), &min, sizeof(max), &max);
	qll += 4 + (4+4+4+2+sizeof(min)+2+sizeof(max));
	cp += 4 + (4+4+4+2+sizeof(min)+2+sizeof(max));

	/* second query */
	set_qce(cp, page, 1, 3, "ab", 5, "keta");
	qll += (4+4+4+2+2+2+5);
	cp += (4+4+4+2+2+2+5);

	ret = osd_command_set_query(&cmd, pid, cid, qll, 4096);
	assert(ret == 0);
	ret = osdemu_cmd_submit(osd, cmd.cdb, buf, qll, &matches,
				&matchlen, sense_out, &senselen_out);
	assert(ret == 0);
	idlist[3] = oid;
	idlist[4] = oid+1;
	idlist[0] = oid+4; 
	idlist[1] = oid+5; 
	idlist[5] = oid+6;
	idlist[2] = oid+7;
	check_results(matches, matchlen, idlist, 6);

	free(matches);
	matches = NULL;
	matchlen = 0;

	/* run intersection of 3 query criteria, with missing min/max */
	qll = 0;

	/* first query */
	memset(buf, 0, 1024);
	cp = buf;
	cp[0] = 0x1; /* INTERSECTION */
	set_qce(&cp[4], page, 1, 2, "a", 3, "zz");
	qll += 4 + (4+4+4+2+2+2+3);
	cp += 4 + (4+4+4+2+2+2+3);

	/* second query */
	min = 140;
	set_htonll(&min, min);
	set_qce(cp, page, 1, sizeof(min), &min, 0, NULL);
	qll += (4+4+4+2+sizeof(min)+2+0);
	cp += (4+4+4+2+sizeof(min)+2+0);

	/* third query */
	set_qce(cp, page, 2, 0, NULL, 6, "alpha");
	qll += (4+4+4+2+0+2+6);
	cp += (4+4+4+2+0+2+6);

	ret = osd_command_set_query(&cmd, pid, cid, qll, 4096);
	assert(ret == 0);
	ret = osdemu_cmd_submit(osd, cmd.cdb, buf, qll, &matches,
				&matchlen, sense_out, &senselen_out);
	assert(ret == 0);
	idlist[0] = oid+1;
	check_results(matches, matchlen, idlist, 1);

	free(matches);
	matches = NULL;
	matchlen = 0;

	/* run intersection of 2 query criteria with empty result */
	qll = 0;

	/* first query */
	memset(buf, 0, 1024);
	cp = buf;
	cp[0] = 0x1; /* INTERSECTION */
	set_qce(&cp[4], page, 1, 3, "aa", 4, "zzz");
	qll += 4 + (4+4+4+2+3+2+4);
	cp += 4 + (4+4+4+2+3+2+4);

	/* second query */
	min = 50;
	max = 80;
	set_htonll(&min, min);
	set_htonll(&max, max);
	set_qce(cp, page, 1, sizeof(min), &min, sizeof(max), &max);
	qll += (4+4+4+2+sizeof(min)+2+sizeof(max));
	cp += (4+4+4+2+sizeof(min)+2+sizeof(max));

	ret = osd_command_set_query(&cmd, pid, cid, qll, 4096);
	assert(ret == 0);
	ret = osdemu_cmd_submit(osd, cmd.cdb, buf, qll, &matches,
				&matchlen, sense_out, &senselen_out);
	assert(ret == 0);
	check_results(matches, matchlen, idlist, 0);

	free(matches);
	matches = NULL;
	matchlen = 0;

	/*
	 * Cleanup.
	 */
	for (i=0; i<10; i++) {
		ret = osd_command_set_remove(&cmd, pid, oid + i);
		assert(ret == 0);
		ret = osdemu_cmd_submit(osd, cmd.cdb, NULL, 0, &data_out,
					&data_out_len, sense_out,
					&senselen_out);
		assert(ret == 0);
	}
	ret = osd_command_set_remove_collection(&cmd, pid, cid, 0);
	assert(ret == 0);
	ret = osdemu_cmd_submit(osd, cmd.cdb, NULL, 0, &data_out,
				&data_out_len, sense_out, &senselen_out);
	assert(ret == 0);

	ret = osd_command_set_remove_partition(&cmd, pid);
	assert(ret == 0);
	ret = osdemu_cmd_submit(osd, cmd.cdb, NULL, 0, &data_out,
				&data_out_len, sense_out, &senselen_out);
	assert(ret == 0);
}

struct test_attr {
	uint8_t type;
	uint64_t oid;
	uint32_t page;
	uint32_t number;
	uint64_t intval;
	uint16_t valen;
	const void *val;
}; 

static void ismember_attr(struct test_attr *attr, size_t sz, uint64_t oid,
			  uint32_t page, uint32_t number, uint64_t valen,
			  const void *val)
{
	size_t i = 0;
	for (i = 0; i < sz; i++) {
		if (attr[i].oid == oid && attr[i].page == page &&
		    attr[i].number == number) {
			assert(valen <= attr[i].valen);
			if (attr[i].type == 1) {
				if (valen == attr[i].valen)
					assert(attr[i].intval == get_ntohll(val));
			} else {
				assert(memcmp(attr[i].val, val, valen) == 0);
			}
			return;
		}
	}
	fprintf(stderr, "unknown attr: oid: %llu, page %u, number %u\n",
		llu(oid), page, number);
	assert(0); /* unknown attr */
}

static void test_oids_with_attr(struct osd_device *osd, uint64_t pid, 
				struct attribute_list *getattr, int numattr,
				uint64_t alloc_len, uint64_t exp_data_out_len,
				uint64_t exp_add_len, uint64_t exp_cont_id,
				uint8_t exp_odf, struct test_attr *attrs,
				size_t attrs_sz)
{
	int ret = 0;
	struct osd_command cmd;
	uint8_t *cp = NULL;
	uint32_t page = 0, number = 0;
	uint64_t data_in_len, data_out_len;
	const void *data_in;
	uint8_t *data_out = NULL;
	uint64_t oid = 0; 
	uint8_t sense_out[OSD_MAX_SENSE];
	uint16_t len = 0;
	int senselen_out;
	uint32_t attr_list_len = 0;

	/* execute list with attr, alloc length less than required */
	ret = osd_command_set_list(&cmd, pid, 0, alloc_len, 0, 1);
	assert(ret == 0);
	ret = osd_command_attr_build(&cmd, getattr, numattr);
	assert(ret == 0);
	data_in = cmd.outdata;
	data_in_len = cmd.outlen;
	ret = osdemu_cmd_submit(osd, cmd.cdb, data_in, data_in_len, &data_out,
				&data_out_len, sense_out, &senselen_out);
	assert(ret == 0);
	cp = data_out;
	assert(data_out_len == exp_data_out_len);
	assert(get_ntohll(cp) == exp_add_len);
	cp += 8;
	assert(get_ntohll(cp) == exp_cont_id);
	cp += 8;
	assert(get_ntohl(cp) == 0);
	cp += 7;
	assert(cp[0] == exp_odf);
	cp += 1;
	oid = 0;
	attr_list_len = 0;
	len = 0;
	data_out_len -= 24;
	while (data_out_len > 0) {
		oid = get_ntohll(cp);
		cp += 12;
		attr_list_len = get_ntohl(cp);
		cp += 4;
		data_out_len -= 16;
		while (attr_list_len > 0) {
			page = get_ntohl(cp);
			cp += 4;
			number = get_ntohl(cp);
			cp += 4;
			len = get_ntohs(cp);
			cp += 2;
			attr_list_len -= (4+4+2);
			data_out_len -= (4+4+2);
			if (len > attr_list_len) {
				len = attr_list_len;
			}
			ismember_attr(attrs, attrs_sz, oid, page, number,
				      len, cp);
			cp += len;
			cp += (roundup8(2+len) - (2+len));
			data_out_len -= len;
			data_out_len -= (roundup8(2+len) - (2+len));
			attr_list_len -= len;
			attr_list_len -= (roundup8(2+len) - (2+len)); 
		}
	}
	free(data_out);
	data_out = NULL;
	osd_command_attr_free(&cmd);
}

void test_list(struct osd_device *osd)
{
	struct osd_command cmd;
	uint64_t pid = PARTITION_PID_LB;
	uint64_t cid = 0;
	uint64_t oid = 0; 
	uint8_t *data_out = NULL;
	uint8_t *cp;
	uint32_t page = 0, number = 0;
	uint64_t data_out_len;
	uint64_t idlist[64];
	uint8_t sense_out[OSD_MAX_SENSE];
	int senselen_out;
	int i, ret;

	/* create partition */
	ret = osd_command_set_create_partition(&cmd, pid);
	assert(ret == 0);
	ret = osdemu_cmd_submit(osd, cmd.cdb, NULL, 0, &data_out,
				&data_out_len, sense_out, &senselen_out);
	assert(ret == 0);

	/* create collection */
	ret = osd_command_set_create_collection(&cmd, pid, cid);
	assert(ret == 0);
	ret = osdemu_cmd_submit(osd, cmd.cdb, NULL, 0, &data_out,
				&data_out_len, sense_out, &senselen_out);
	assert(ret == 0);

	/* create 6 objects */
	ret = osd_command_set_create(&cmd, pid, 0, 6);
	assert(ret == 0);
	ret = osdemu_cmd_submit(osd, cmd.cdb, NULL, 0, &data_out, 
				&data_out_len, sense_out, &senselen_out);
	assert(ret == 0);

	/* create another collection */
	ret = osd_command_set_create_collection(&cmd, pid, cid);
	assert(ret == 0);
	ret = osdemu_cmd_submit(osd, cmd.cdb, NULL, 0, &data_out,
				&data_out_len, sense_out, &senselen_out);
	assert(ret == 0);

	/* create 4 objects */
	ret = osd_command_set_create(&cmd, pid, 0, 4);
	assert(ret == 0);
	ret = osdemu_cmd_submit(osd, cmd.cdb, NULL, 0, &data_out, 
				&data_out_len, sense_out, &senselen_out);
	assert(ret == 0);

	/* set attributes on userobjects */
	page = USEROBJECT_PG + LUN_PG_LB;
	number = 1;
	oid = COLLECTION_OID_LB + 1;
	struct test_attr attrs[] = {
		{1, oid, page, number, 1, 8, NULL},
		{1, oid, page+1, number+1, 768, 8, NULL},
		{2, oid, page+2, number+2, 0, 5, "sudo"}, 
		{1, oid+1, page+1, number+1, 56, 8, NULL},
		{1, oid+1, page+2, number+2, 68, 8, NULL},
		{2, oid+2, page+2, number+2, 0, 9, "deadbeef"}, 
		{1, oid+3, page+3, number+3, 1, 8, NULL},
		{1, oid+3, page+1, number+1, 111, 8, NULL},
		{2, oid+3, page+4, number+4, 0, 5, "sudo"}, 
		{1, oid+3, page+2, number+2, 11, 8, NULL},
		{1, oid+3, page+5, number+5, 111111, 8, NULL},
		{2, oid+4, page+4, number+4, 0, 6, "milli"}, 
		{2, oid+4, page+5, number+5, 0, 10, "kilometer"},
		{2, oid+4, page+3, number+3, 0, 11, "hectameter"},
		{2, oid+5, page+1, number+1, 0, 12, "zzzzzzhhhhh"}, 
		{2, oid+5, page+2, number+2, 0, 2, "b"},
		{1, oid+5, page+3, number+3, 6, 8, NULL},
		{1, oid+7, page+1, number+1, 486, 8, NULL}, 
		{1, oid+7, page+4, number+4, 586, 8, NULL},
		{1, oid+7, page+2, number+2, 686, 8, NULL},
		{1, oid+8, page, number, 4, 8, NULL},
		{2, oid+9, page+1, number+1, 0, 14, "setting these"}, 
		{2, oid+9, page+2, number+2, 0, 11, "attributes"},
		{2, oid+9, page+3, number+3, 0, 8, "made me"},
		{2, oid+9, page+4, number+4, 0, 12, "mad! really"}, 
		{1, oid+10, page+1, number+1, 1234567890, 8, NULL},
		{2, oid+10, page, number, 0, 6, "DelTa"},
	};
	for (i = 0; i < ARRAY_SIZE(attrs); i++) {
		if (attrs[i].type == 1) {
			set_attr_int(osd, pid, attrs[i].oid, attrs[i].page,
				     attrs[i].number, attrs[i].intval);
		} else {
			set_attr_val(osd, pid, attrs[i].oid, attrs[i].page,
				     attrs[i].number, attrs[i].val,
				     attrs[i].valen);
		}
	}

	/* set some attributes on collections */
	page = COLLECTION_PG + LUN_PG_LB;
	cid = COLLECTION_OID_LB;
	set_attr_int(osd, pid, cid,   page, 1, 1);
	set_attr_int(osd, pid, cid+7, page, 2, 2);

	/* execute list command. get only oids first */
	ret = osd_command_set_list(&cmd, pid, 0, 4096, 0, 0);
	assert(ret == 0);
	ret = osdemu_cmd_submit(osd, cmd.cdb, NULL, 0, &data_out,
				&data_out_len, sense_out, &senselen_out);
	assert(ret == 0);
	cp = data_out;
	assert(get_ntohll(cp) == 10*8+16);
	assert(data_out_len == 10*8+24);
	cp += 8;
	assert(get_ntohll(cp) == 0);
	cp += 8;
	assert(get_ntohl(cp) == 0);
	cp += 7;
	assert(cp[0] == (0x21 << 2));
	cp += 1;
	data_out_len -= 24;
	oid = COLLECTION_OID_LB + 1;
	for (i = 0; i < 6; i++)
		idlist[i] = oid + i;
	oid = COLLECTION_OID_LB + 1 + i + 1;
	for (i = 0; i < 4; i++)
		idlist[6+i] = oid + i;
	while (data_out_len > 0) {
		assert(ismember(get_ntohll(cp), idlist, 10));
		cp += 8;
		data_out_len -= 8;
	}
	free(data_out);
	data_out = NULL;
	osd_command_attr_free(&cmd);

	/* execute list command with less space */
	ret = osd_command_set_list(&cmd, pid, 0, 72, 0, 0);
	assert(ret == 0);
	ret = osdemu_cmd_submit(osd, cmd.cdb, NULL, 0, &data_out,
				&data_out_len, sense_out, &senselen_out);
	assert(ret == 0);
	cp = data_out;
	assert(get_ntohll(cp) == 10*8+16);
	assert(data_out_len == 72);
	cp += 8;
	assert(get_ntohll(cp) == COLLECTION_OID_LB + 8);
	cp += 8;
	assert(get_ntohl(cp) == 0);
	cp += 7;
	assert(cp[0] == (0x21 << 2));
	cp += 1;
	data_out_len -= 24;
	oid = COLLECTION_OID_LB + 1;
	for (i = 0; i < 6; i++)
		idlist[i] = oid + i;
	for (i = 0; i < 4; i++)
		idlist[6+i] = 0;
	while (data_out_len > 0) {
		assert(ismember(get_ntohll(cp), idlist, 10));
		cp += 8;
		data_out_len -= 8;
	}
	free(data_out);
	data_out = NULL;
	osd_command_attr_free(&cmd);

	page = USEROBJECT_PG + LUN_PG_LB;
	number = 1;
	struct attribute_list getattr[] = {
		{ATTR_GET, page, number, NULL, 0, 0},
		{ATTR_GET, page+1, number+1, NULL, 0, 0},
		{ATTR_GET, page+2, number+2, NULL, 0, 0},
		{ATTR_GET, page+3, number+3, NULL, 0, 0},
		{ATTR_GET, page+4, number+4, NULL, 0, 0},
		{ATTR_GET, page+5, number+5, NULL, 0, 0},
	};

	/* execute list with attr */
	test_oids_with_attr(osd, pid, getattr, 6, 4096, 792, 784, 0, 
			    (0x22 << 2), attrs, ARRAY_SIZE(attrs));
	/* execute list with attr, alloc length less than required */
	test_oids_with_attr(osd, pid, getattr, 6, 200, 200, 784, 65539, 
			    (0x22 << 2), attrs, ARRAY_SIZE(attrs));
	/* execute list with attr, alloc length less than required */
	test_oids_with_attr(osd, pid, getattr, 6, 208, 208, 784, 65539, 
			    (0x22 << 2), attrs, ARRAY_SIZE(attrs));
	/* execute list with attr, alloc length less than required */
	test_oids_with_attr(osd, pid, getattr, 6, 216, 208, 784, 65539, 
			    (0x22 << 2), attrs, ARRAY_SIZE(attrs));
	/* execute list with attr, alloc length less than required */
	test_oids_with_attr(osd, pid, getattr, 6, 544, 536, 784, 65544, 
			    (0x22 << 2), attrs, ARRAY_SIZE(attrs));
	/* execute list with attr, alloc length less than required */
	test_oids_with_attr(osd, pid, getattr, 6, 688, 688, 784, 65546, 
			    (0x22 << 2), attrs, ARRAY_SIZE(attrs));
	/* execute list with attr, alloc length less than required */
	test_oids_with_attr(osd, pid, getattr, 6, 680, 680, 784, 65546, 
			    (0x22 << 2), attrs, ARRAY_SIZE(attrs));

	/* clean up */
	oid = USEROBJECT_OID_LB;
	for (i=0; i<12; i++) {
		if (i == 0 || i == 7)
			continue;
		ret = osd_command_set_remove(&cmd, pid, oid + i);
		assert(ret == 0);
		ret = osdemu_cmd_submit(osd, cmd.cdb, NULL, 0, &data_out,
					&data_out_len, sense_out,
					&senselen_out);
		assert(ret == 0);
	}
	cid = COLLECTION_OID_LB;
	ret = osd_command_set_remove_collection(&cmd, pid, cid, 0);
	assert(ret == 0);
	ret = osdemu_cmd_submit(osd, cmd.cdb, NULL, 0, &data_out,
				&data_out_len, sense_out, &senselen_out);
	assert(ret == 0);
	cid = COLLECTION_OID_LB + 7;
	ret = osd_command_set_remove_collection(&cmd, pid, cid, 0);
	assert(ret == 0);
	ret = osdemu_cmd_submit(osd, cmd.cdb, NULL, 0, &data_out,
				&data_out_len, sense_out, &senselen_out);
	assert(ret == 0);

	ret = osd_command_set_remove_partition(&cmd, pid);
	assert(ret == 0);
	ret = osdemu_cmd_submit(osd, cmd.cdb, NULL, 0, &data_out,
				&data_out_len, sense_out, &senselen_out);
	assert(ret == 0);
}

static void test_attr_vals(uint8_t *cp, struct attribute_list *attrs, 
			   size_t sz)
{
	size_t i = 0;
	uint32_t page = 0;
	uint32_t num = 0;
	uint16_t len = 0;
	uint32_t list_len = 0;

	assert((cp[0] & 0x0F) == 0x9);
	cp += 4;
	list_len = get_ntohl(cp);
	cp += 4;
	while (list_len > 0) {
		page = get_ntohl(cp);
		cp += 4;
		num = get_ntohl(cp);
		cp += 4;
		len = get_ntohs(cp);
		cp += 2;
		for (i = 0; i < sz; i++) {
			if (!(attrs[i].page==page && attrs[i].number==num)) 
				continue;

			assert(len == attrs[i].len);
			if (len == 8) {
				assert(get_ntohll(attrs[i].val) ==
				       get_ntohll(cp));
			} else if (len != 0) {
				assert(memcmp(attrs[i].val, cp, len) == 0);
			}
			break;
		}
		assert(i < sz);
		if (len == 0) {
			cp += (roundup8(10) - 10);
			list_len -= roundup8(4+4+2);
		} else {
			cp += len;
			cp += (roundup8(2+len) - (2+len));
			list_len -= roundup8(4+4+2+len);
		}
	}
	assert(list_len == 0);
}

void test_set_member_attributes(struct osd_device *osd)
{
	struct osd_command cmd;
	uint64_t pid = PARTITION_PID_LB;
	uint64_t cid = COLLECTION_OID_LB;
	uint64_t oid = 0; 
	uint8_t *data_out = NULL;
	uint32_t page = 0;
	const void *data_in;
	uint64_t data_out_len, data_in_len;
	uint8_t sense_out[OSD_MAX_SENSE];
	int senselen_out;
	int i, ret;

	data_out = NULL;
	data_out_len = 0;

	/* create partition */
	ret = osd_command_set_create_partition(&cmd, pid);
	assert(ret == 0);
	ret = osdemu_cmd_submit(osd, cmd.cdb, NULL, 0, &data_out,
				&data_out_len, sense_out, &senselen_out);
	assert(ret == 0);

	/* create collection */
	ret = osd_command_set_create_collection(&cmd, pid, cid);
	assert(ret == 0);
	ret = osdemu_cmd_submit(osd, cmd.cdb, NULL, 0, &data_out,
				&data_out_len, sense_out, &senselen_out);
	assert(ret == 0);

	/* create 100 objects */
	ret = osd_command_set_create(&cmd, pid, 0, 100);
	assert(ret == 0);
	ret = osdemu_cmd_submit(osd, cmd.cdb, NULL, 0, &data_out, 
				&data_out_len, sense_out, &senselen_out);
	assert(ret == 0);
	assert(osd->ccap.oid == (USEROBJECT_OID_LB + 100));

	/* put odd objects into the collection */
	oid = USEROBJECT_OID_LB + 1;
	for (i = 0; i < 100; i += 2) {
		uint64_t attrval;
		struct attribute_list attr = {
			.type = ATTR_SET,
			.page = USER_COLL_PG,
			.number = 1,
			.len = sizeof(attrval),
			.val = &attrval,
		};
		set_htonll(&attrval, cid);
		ret = osd_command_set_set_attributes(&cmd, pid, oid + i);
		assert(ret == 0);
		ret = osd_command_attr_build(&cmd, &attr, 1);
		assert(ret == 0);
		ret = osdemu_cmd_submit(osd, cmd.cdb, cmd.outdata, cmd.outlen,
					&data_out, &data_out_len,
					sense_out, &senselen_out);
		assert(ret == 0);
		osd_command_attr_free(&cmd);
	}

	/* set attr on collection members */
	uint64_t val1 = 123454321, val2 = 987654, val3 = 59999999;
	char str1[MAXNAMELEN], str2[MAXNAMELEN], str3[MAXNAMELEN];
	set_htonll(&val1, val1);
	set_htonll(&val2, val2);
	set_htonll(&val3, val3);
	sprintf(str1, "GoMtI");
	sprintf(str2, "DeViL");
	sprintf(str3, "homeopath");
	page = USEROBJECT_PG + LUN_PG_LB;
	struct attribute_list attrs[] = {
		{ATTR_SET, page+1, 2, str1, strlen(str1)+1, 0},
		{ATTR_SET, page+21, 4, &val1, sizeof(val1), 0},
		{ATTR_SET, page+5, 55, &val2, sizeof(val2), 0},
		{ATTR_SET, page+666, 66, str2, strlen(str2)+1, 0},
		{ATTR_SET, page+10, 10, str3, strlen(str3)+1, 0},
		{ATTR_SET, page+2, 3, &val3, sizeof(val3), 0},
	};
	ret = osd_command_set_set_member_attributes(&cmd, pid, cid);
	assert(ret == 0);
	ret = osd_command_attr_build(&cmd, attrs, 6);
	assert(ret == 0);
	ret = osdemu_cmd_submit(osd, cmd.cdb, cmd.outdata, cmd.outlen, 
				&data_out, &data_out_len, sense_out, 
				&senselen_out);
	assert(ret == 0);
	osd_command_attr_free(&cmd);

	/* randomly select 5 objects, get their attrs and test */
	for (i = 0; i < 6; i++) {
		attrs[i].type = ATTR_GET;
	}
	srand(time(0));
	for (i = 0; i < 5; i++) {
		int r = (int)(50.0 * (rand()/(RAND_MAX+1.0)));
		oid = USEROBJECT_OID_LB + 1 + 2*r;
		ret = osd_command_set_get_attributes(&cmd, pid, oid);
		assert(ret == 0);
		ret = osd_command_attr_build(&cmd, attrs, 6);
		assert(ret == 0);
		data_in = cmd.outdata;
		data_in_len = cmd.outlen;
		ret = osdemu_cmd_submit(osd, cmd.cdb, data_in, data_in_len,
					&data_out, &data_out_len, sense_out,
					&senselen_out);
		assert(ret == 0);
		test_attr_vals(data_out, attrs, 6);
		osd_command_attr_free(&cmd);
	}

	free(data_out);
	data_out = NULL;
	data_out_len = 0;

	/* clean up */
	oid = USEROBJECT_OID_LB + 1;
	for (i=0; i<100; i++) {
		ret = osd_command_set_remove(&cmd, pid, oid + i);
		assert(ret == 0);
		ret = osdemu_cmd_submit(osd, cmd.cdb, NULL, 0, &data_out,
					&data_out_len, sense_out,
					&senselen_out);
		assert(ret == 0);
	}
	cid = COLLECTION_OID_LB;
	ret = osd_command_set_remove_collection(&cmd, pid, cid, 0);
	assert(ret == 0);
	ret = osdemu_cmd_submit(osd, cmd.cdb, NULL, 0, &data_out,
				&data_out_len, sense_out, &senselen_out);
	ret = osd_command_set_remove_partition(&cmd, pid);
	assert(ret == 0);
	ret = osdemu_cmd_submit(osd, cmd.cdb, NULL, 0, &data_out,
				&data_out_len, sense_out, &senselen_out);
	assert(ret == 0);
}

void test_atomics(struct osd_device *osd)
{
	int ret = 0;
	struct osd_command cmd;
	int senselen_out;
	uint8_t sense_out[OSD_MAX_SENSE];
	uint8_t *cp = NULL;
	uint8_t *data_out = NULL;
	void *data_in = NULL;
	uint64_t data_out_len, data_in_len;

	/* create partition + empty getpage_setlist */
	ret = osd_command_set_create_partition(&cmd, PARTITION_PID_LB);
	assert(ret == 0);
	ret = osdemu_cmd_submit(osd, cmd.cdb, NULL, 0, &data_out,
				&data_out_len, sense_out, &senselen_out);
	assert(ret == 0);

	/* create 1 object */
	ret = osd_command_set_create(&cmd, USEROBJECT_PID_LB,
				     USEROBJECT_OID_LB, 1); 
	assert(ret == 0);
	ret = osdemu_cmd_submit(osd, cmd.cdb, NULL, 0, &data_out, &data_out_len,
				sense_out, &senselen_out);
	assert(ret == 0);

	/* cas */
	ret = osd_command_set_cas(&cmd, USEROBJECT_PID_LB, USEROBJECT_OID_LB,
				  8UL, 0);
	assert(ret == 0);
	data_in = Malloc(1024);
	assert(data_in != NULL);
	cp = data_in;
	set_htonll(&cp[0], 0UL);
	set_htonll(&cp[8], 5UL);
	data_in_len = 16;
	data_out = NULL;
	data_out_len = 0;
	ret = osdemu_cmd_submit(osd, cmd.cdb, data_in, data_in_len, &data_out, 
				&data_out_len, sense_out, &senselen_out);
	assert(ret == 0);
	assert(data_out != NULL);
	assert(get_ntohll(&data_out[0]) == 0UL);

	free(data_out);
	data_out = NULL;
	data_out_len = 0;

	ret = osd_command_set_cas(&cmd, USEROBJECT_PID_LB, USEROBJECT_OID_LB,
				  8UL, 0);
	assert(ret == 0);
	cp = data_in;
	set_htonll(&cp[0], 5UL);
	set_htonll(&cp[8], 0UL);
	data_in_len = 16;
	ret = osdemu_cmd_submit(osd, cmd.cdb, data_in, data_in_len, &data_out, 
				&data_out_len, sense_out, &senselen_out);
	assert(ret == 0);
	assert(data_out != NULL);
	assert(get_ntohll(&data_out[0]) == 5UL);

	free(data_out);
	data_out = NULL;
	data_out_len = 0;

	/* fa */
	ret = osd_command_set_fa(&cmd, USEROBJECT_PID_LB, USEROBJECT_OID_LB,
				 8UL, 0);
	assert(ret == 0);
	cp = data_in;
	set_htonll(&cp[0], 4UL);
	data_in_len = 8;
	ret = osdemu_cmd_submit(osd, cmd.cdb, data_in, data_in_len, &data_out, 
				&data_out_len, sense_out, &senselen_out);
	assert(ret == 0);
	assert(data_out != NULL);
	assert(get_ntohll(&data_out[0]) == 0UL);

	free(data_out);
	data_out = NULL;
	data_out_len = 0;
	
	ret = osd_command_set_fa(&cmd, USEROBJECT_PID_LB, USEROBJECT_OID_LB,
				 8UL, 0);
	assert(ret == 0);
	cp = data_in;
	set_htonll(&cp[0], 16UL);
	data_in_len = 8;
	ret = osdemu_cmd_submit(osd, cmd.cdb, data_in, data_in_len, &data_out, 
				&data_out_len, sense_out, &senselen_out);
	assert(ret == 0);
	assert(data_out != NULL);
	assert(get_ntohll(&data_out[0]) == 4UL);

	free(data_out);
	data_out = NULL;
	data_out_len = 0;

	ret = osd_command_set_fa(&cmd, USEROBJECT_PID_LB, USEROBJECT_OID_LB,
				 8UL, 0);
	assert(ret == 0);
	cp = data_in;
	set_htonll(&cp[0], -20L);
	data_in_len = 8;
	ret = osdemu_cmd_submit(osd, cmd.cdb, data_in, data_in_len, &data_out, 
				&data_out_len, sense_out, &senselen_out);
	assert(ret == 0);
	assert(data_out != NULL);
	assert(get_ntohll(&data_out[0]) == 20UL);

	free(data_out);
	data_out = NULL;
	data_out_len = 0;

	ret = osd_command_set_fa(&cmd, USEROBJECT_PID_LB, USEROBJECT_OID_LB,
				 8UL, 0);
	assert(ret == 0);
	cp = data_in;
	set_htonll(&cp[0], 1L);
	data_in_len = 8;
	ret = osdemu_cmd_submit(osd, cmd.cdb, data_in, data_in_len, &data_out, 
				&data_out_len, sense_out, &senselen_out);
	assert(ret == 0);
	assert(data_out != NULL);
	assert(get_ntohll(&data_out[0]) == 0UL);

	free(data_out);
	data_out = NULL;
	data_out_len = 0;

	free(data_in);

	/* gen_cas */
	ret = osd_command_set_gen_cas(&cmd, USEROBJECT_PID_LB, 
				      USEROBJECT_OID_LB);
	assert(ret == 0);
	char str1[MAXNAMELEN];
	sprintf(str1, "some arbit string");
	struct attribute_list attr[] = {
		{ATTR_SET, USEROBJECT_PG+LUN_PG_LB, 1, NULL, 0, 0},
		{ATTR_SET, USEROBJECT_PG+LUN_PG_LB, 1, str1, strlen(str1)+1,
			0
		},
		{ATTR_RESULT, USEROBJECT_PG+LUN_PG_LB, 1, NULL, 0, 0}
	};
	ret = osd_command_attr_build(&cmd, attr, 3);
	assert(ret == 0);
	ret = osdemu_cmd_submit(osd, cmd.cdb, cmd.outdata, cmd.outlen, 
				&data_out, &data_out_len, sense_out, 
				&senselen_out);
	assert(ret == 0);
	assert(data_out != NULL);
	assert(data_out_len == 24);
	attr[2].len = 0;
	test_attr_vals(data_out, &attr[2], 1);
	osd_command_attr_free(&cmd);
	free(data_out);
	data_out = NULL;
	data_out_len = 0;

	ret = osd_command_set_gen_cas(&cmd, USEROBJECT_PID_LB, 
				      USEROBJECT_OID_LB);
	assert(ret == 0);
	char str2[MAXNAMELEN];
	sprintf(str2, "a diff str");
	struct attribute_list attr1[] = {
		{ATTR_SET, USEROBJECT_PG+LUN_PG_LB, 1, str1, strlen(str1)+1,
			0
		},
		{ATTR_SET, USEROBJECT_PG+LUN_PG_LB, 1, str2, strlen(str2)+1,
			0
		},
		{ATTR_RESULT, USEROBJECT_PG+LUN_PG_LB, 1, str1, strlen(str1)+1,
			0
		}
	};
	ret = osd_command_attr_build(&cmd, attr1, 3);
	assert(ret == 0);
	ret = osdemu_cmd_submit(osd, cmd.cdb, cmd.outdata, cmd.outlen, 
				&data_out, &data_out_len, sense_out, 
				&senselen_out);
	assert(ret == 0);
	assert(data_out != NULL);
	test_attr_vals(data_out, &attr1[2], 1);
	osd_command_attr_free(&cmd);
	free(data_out);
	data_out = NULL;
	data_out_len = 0;

	/* cond setattr */
	ret = osd_command_set_cond_setattr(&cmd, USEROBJECT_PID_LB,
					   USEROBJECT_OID_LB);
	assert(ret == 0);
	char str3[MAXNAMELEN];
	sprintf(str3, "setattr str");
	struct attribute_list attr2[] = {
		{ATTR_SET, USEROBJECT_PG+LUN_PG_LB, 1, str2, strlen(str2)+1,
			0
		}, /* cmp */
		{ATTR_SET, USEROBJECT_PG+LUN_PG_LB, 1, str1, strlen(str1)+1,
			0
		}, /* swap */
		{ATTR_SET, USEROBJECT_PG+LUN_PG_LB+100, 23, str3, 
			strlen(str3)+1, 0
		},
		{ATTR_RESULT, USEROBJECT_PG+LUN_PG_LB, 1, str2, strlen(str2)+1,
			0
		}
	};
	ret = osd_command_attr_build(&cmd, attr2, 4);
	assert(ret == 0);
	ret = osdemu_cmd_submit(osd, cmd.cdb, cmd.outdata, cmd.outlen, 
				&data_out, &data_out_len, sense_out, 
				&senselen_out);
	assert(ret == 0);
	assert(data_out != NULL);
	test_attr_vals(data_out, &attr2[3], 1);
	osd_command_attr_free(&cmd);
	free(data_out);
	data_out = NULL;
	data_out_len = 0;

	attr2[2].type = ATTR_GET;
	ret = osd_command_set_get_attributes(&cmd, USEROBJECT_PID_LB,
					     USEROBJECT_OID_LB);
	assert(ret == 0);
	ret = osd_command_attr_build(&cmd, &attr2[2], 1);
	assert(ret == 0);
	ret = osdemu_cmd_submit(osd, cmd.cdb, cmd.outdata, cmd.outlen, 
				&data_out, &data_out_len, sense_out, 
				&senselen_out);
	assert(ret == 0);
	assert(data_out != NULL);
	test_attr_vals(data_out, &attr2[2], 1);
	osd_command_attr_free(&cmd);
	free(data_out);
	data_out = NULL;
	data_out_len = 0;

	/* clean up */
	ret = osd_command_set_remove(&cmd, USEROBJECT_PID_LB, 
				     USEROBJECT_OID_LB);
	assert(ret == 0);
	ret = osdemu_cmd_submit(osd, cmd.cdb, NULL, 0, &data_out,
				&data_out_len, sense_out, &senselen_out);
	ret = osd_command_set_remove_partition(&cmd, PARTITION_PID_LB);
	assert(ret == 0);
	ret = osdemu_cmd_submit(osd, cmd.cdb, NULL, 0, &data_out,
				&data_out_len, sense_out, &senselen_out);
	assert(ret == 0);

}

int main()
{
	int ret = 0;
	const char *root = "/tmp/osd/";
	struct osd_device osd;

	system("rm -rf /tmp/osd");
	ret = osd_open(root, &osd);
	assert(ret == 0);
	
	test_set_one_attr(&osd); 
	/* test_partition(&osd); */
	/* test_create(&osd); */
	/* test_query(&osd); */
	/* test_list(&osd); */
	/* test_set_member_attributes(&osd); */
	/* test_atomics(&osd); */

	ret = osd_close(&osd);
	assert(ret == 0);

	return 0;
}
