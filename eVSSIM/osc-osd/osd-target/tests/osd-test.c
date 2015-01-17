/*
 * Basic OSD tests.
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

#include "osd.h"
#include "db.h"
#include "attr.h"
#include "obj.h"
#include "coll.h"
#include "osd-util/osd-util.h"
#include "osd-util/osd-sense.h"
#include "target-sense.h"

static const char *dfiles = "dfiles";

static inline void get_dfile_name(char *path, const char *root,
				  uint64_t pid, uint64_t oid)
{
#ifdef PVFS_OSD_INTEGRATED
	/* go look in PVFS bstreams for file data (eventually) */
	sprintf(path, "%s/%08llx/bstreams/%.8llu/%08llx.bstream", root,
	        llu(pid), llu(oid % 64), llu(oid));
	printf("root = %s collid = 0x%llx\n", root, llu(pid));
#else
	sprintf(path, "%s/%s/%02x/%llx.%llx", root, dfiles,
		(uint8_t)(oid & 0xFFUL), llu(pid), llu(oid));
#endif
}

static void test_osd_create(struct osd_device *osd)
{
	int ret = 0;
	void *sense = Calloc(1, 1024);
	uint32_t cdb_cont_len = 0;

	/* invalid pid/oid, test must fail */
	ret = osd_create(osd, 0, 1, 0, cdb_cont_len, sense);
	assert(ret != 0);

	/* invalid oid test must fail */
	ret = osd_create(osd, USEROBJECT_PID_LB, 1, 0, cdb_cont_len, sense);
	assert(ret != 0);

	/* num > 1 cannot request oid, test must fail */
	ret = osd_create(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB, 2, cdb_cont_len, sense);
	assert(ret != 0);

	ret = osd_create_partition(osd, PARTITION_PID_LB, cdb_cont_len, sense);
	assert(ret == 0);

	ret = osd_create(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB, 0, cdb_cont_len, sense);
	assert(ret == 0);

	ret = osd_remove(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB, cdb_cont_len, sense);
	assert(ret == 0);

	/* remove non-existing object, test must fail */
	ret = osd_remove(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB, cdb_cont_len, sense);
	assert(ret != 0);

	ret = osd_remove_partition(osd, PARTITION_PID_LB, cdb_cont_len, sense);
	assert(ret == 0);

	free(sense);
}


static void test_osd_set_attributes(struct osd_device *osd)
{
	int ret = 0;
	void *sense = Calloc(1, 1024);
	void *val = Calloc(1, 1024);
	uint32_t cdb_cont_len = 0;

	ret = osd_create_partition(osd, PARTITION_PID_LB, cdb_cont_len, sense);
	assert(ret == 0);
	ret = osd_create(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB, 0, cdb_cont_len, sense);
	assert(ret == 0);

	/* setting root attr must fail */
	ret = osd_set_attributes(osd, ROOT_PID, ROOT_OID, 0, 0,
				 NULL, 0, TRUE, cdb_cont_len, sense);
	assert(ret != 0);

	/* unsettable page modification must fail */
	ret = osd_set_attributes(osd, PARTITION_PID_LB, PARTITION_OID, 0, 0,
				 NULL, 0, TRUE, cdb_cont_len, sense);
	assert(ret != 0);

	/* unsettable collection page must fail */
	ret = osd_set_attributes(osd, COLLECTION_PID_LB, COLLECTION_OID_LB, 0,
				 0, NULL, 0, TRUE, cdb_cont_len, sense);
	assert(ret != 0);

	/* unsettable userobject page must fail */
	ret = osd_set_attributes(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB, 0,
				 0, NULL, 0, TRUE, cdb_cont_len, sense);
	assert(ret != 0);

	/* info attr < 40 bytes, test must fail */
	sprintf(val, "This is test, long test more than forty bytes");
	ret = osd_set_attributes(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB,
				 USEROBJECT_PG + LUN_PG_LB, ATTRNUM_INFO, val,
				 strlen(val)+1, TRUE, cdb_cont_len, sense);
	assert(ret != 0);

	/* this test is normal setattr, must succeed */
	sprintf(val, "Madhuri Dixit");
	ret = osd_set_attributes(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB,
				 USEROBJECT_PG + LUN_PG_LB, 1, val,
				 strlen(val)+1, TRUE, cdb_cont_len, sense);
	assert(ret == 0);

	ret = osd_remove(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB, cdb_cont_len, sense);
	assert(ret == 0);

	ret = osd_remove_partition(osd, PARTITION_PID_LB, cdb_cont_len, sense);
	assert(ret == 0);

	free(sense);
	free(val);
}

static void test_osd_format(struct osd_device *osd)
{
	int ret = 0;
	void *sense = Calloc(1, 1024);
	uint32_t cdb_cont_len = 0;

	ret = osd_format_osd(osd, 0, cdb_cont_len, sense);
	assert(ret == 0);

	free(sense);
}

static void test_osd_clear(struct osd_device *osd)
{
        int ret = 0;
	uint8_t *sense = Calloc(1, 1024);
	uint32_t cdb_cont_len = 0;
     	void *wrbuf = Calloc(1, 256);
	char path[MAXNAMELEN];
	struct stat sb;
		
	ret = osd_create_partition(osd, PARTITION_PID_LB, cdb_cont_len, sense);
	assert(ret == 0);
	ret = osd_create(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB, 0, cdb_cont_len, sense);
	assert(ret == 0);

	sprintf(wrbuf, "Testing osd_clear command\n");
	ret = osd_write(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB,
			strlen(wrbuf)+1, 0, wrbuf, cdb_cont_len, sense, DDT_CONTIG);
	assert(ret == 0);
    
	get_dfile_name(path, osd->root, USEROBJECT_PID_LB, USEROBJECT_OID_LB);

	ret = stat(path, &sb);
	assert(ret == 0);
	
	/* Clear all */
	ret = osd_clear(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB,
			strlen(wrbuf), 0, cdb_cont_len, sense);
	assert(ret == 0 && sb.st_size == (long)strlen(wrbuf)+1);
   	
	/* Clear none */
	ret = osd_clear(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB,
		        0, 0, cdb_cont_len, sense);
	assert(ret == 0 && sb.st_size == (long)strlen(wrbuf)+1);

	/* Clear len or offset > userlength */
	ret = osd_clear(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB,
		        5, strlen(wrbuf)+1, cdb_cont_len, sense);
	assert(ret == 0);
	ret=stat(path, &sb);
	assert(ret == 0 && sb.st_size == (long)strlen(wrbuf)+6);

	free(sense);
	free(wrbuf);
   	
}

static void test_osd_punch(struct osd_device *osd)
{
        int ret = 0;
	uint8_t *sense = Calloc(1, 1024);
	uint32_t cdb_cont_len = 0;
	void *wrbuf = Calloc(1, 256);
	char path[MAXNAMELEN];
	struct stat sb;
	
	ret = osd_create_partition(osd, PARTITION_PID_LB, cdb_cont_len, sense);
	assert(ret == 0);
	
	ret = osd_create(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB, 0, cdb_cont_len, sense);
	assert(ret == 0);

	sprintf(wrbuf, "Testing osd_punch command\n");
	ret = osd_write(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB,
			strlen(wrbuf)+1, 0, wrbuf, cdb_cont_len, sense, DDT_CONTIG);
	assert(ret == 0);

	get_dfile_name(path, osd->root, USEROBJECT_PID_LB, USEROBJECT_OID_LB);


	/* Punch All */
	ret = osd_punch(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB,
			27, 0, cdb_cont_len, sense);
	assert(ret == 0);
	ret = stat(path, &sb);
	assert(ret == 0 && sb.st_size == 0);


	/* Illegal Punch */
	sprintf(wrbuf, "Testing osd_punch command\n");
	ret = osd_write(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB,
			strlen(wrbuf)+1, 0, wrbuf, cdb_cont_len, sense, DDT_CONTIG);
	assert(ret == 0);
	
	ret = osd_punch(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB,
			1, 28, cdb_cont_len, sense);
	assert(ret != 0);
	ret = stat(path, &sb);
	assert(ret == 0 && sb.st_size == (long)strlen(wrbuf)+1);


	/* Punch with len=0 */

	ret = osd_punch(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB,
			0, 0, cdb_cont_len, sense);
	assert(ret == 0);
	ret = stat(path, &sb);
	assert(ret == 0 && sb.st_size == (long)strlen(wrbuf)+1);


	/* Special Case */
	sprintf(wrbuf, "Testing osd_punch command\n");
	
	ret = osd_write(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB,
			strlen(wrbuf)+1, 0, wrbuf, cdb_cont_len, sense, DDT_CONTIG);
	assert(ret == 0);

	ret = osd_punch(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB,
			strlen(wrbuf), strlen(wrbuf)-5, cdb_cont_len, sense);
	assert(ret == 0);
	
	ret = stat(path, &sb);
	assert(ret == 0 && sb.st_size == (long)strlen(wrbuf)-5);


	/* Punch Regular */
	sprintf(wrbuf, "Testing osd_punch command\n");
	ret = osd_write(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB,
			strlen(wrbuf)+1, 0, wrbuf, cdb_cont_len, sense, DDT_CONTIG);
	assert(ret == 0);

	ret = osd_punch(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB,
			8, 0, cdb_cont_len, sense);
	assert(ret == 0);
	ret = stat(path, &sb);
	assert(ret == 0 && sb.st_size == (long)strlen(wrbuf)+1-8);

	free(sense);
	free(wrbuf);
}


static void test_osd_flush(struct osd_device *osd)
{
        int ret = 0;
	uint8_t *sense = Calloc(1, 1024);
	uint32_t cdb_cont_len = 0;
	void *wrbuf = Calloc(1, 256);
	char path[MAXNAMELEN];
	
		
	ret = osd_create_partition(osd, PARTITION_PID_LB, cdb_cont_len, sense);
	assert(ret == 0);
	ret = osd_create(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB, 0, cdb_cont_len, sense);
	assert(ret == 0);

	sprintf(wrbuf, "Testing osd_punch command\n");
	ret = osd_write(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB,
			strlen(wrbuf)+1, 0, wrbuf, cdb_cont_len, sense, DDT_CONTIG);
	assert(ret == 0);
    
	get_dfile_name(path, osd->root, USEROBJECT_PID_LB, USEROBJECT_OID_LB);
	
	/* flush_scope = 0, non-range based data flush, offset & len disregarded */
	ret = osd_flush(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB, 0, 0, 0, cdb_cont_len, sense);
	assert(ret == 0);
	
	/* flush_scope =2, illegal case must fail */
	ret = osd_flush(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB, 5, 30, 2, cdb_cont_len, sense);
	assert(ret != 0);

	/* flush scope =2, ranged based data flush */
	ret = osd_flush(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB, 7, 0, 2, cdb_cont_len, sense);
	assert(ret == 0);

	/* flush scope = 2, special case data flush */
	ret = osd_flush(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB, 20, 10, 2, cdb_cont_len, sense);
	assert(ret == 0);

	free(sense);
	free(wrbuf);
}

static void test_osd_read_map(struct osd_device *osd)
{
  int ret = 0;
	uint8_t *sense = Calloc(1, 1024);
	uint32_t cdb_cont_len = 0;
	void *wrbuf = Calloc(1, 256);
	uint8_t *outdata = Calloc(1, 1024);
       	uint64_t used_outlen = 0;
	char path[MAXNAMELEN];
	uint16_t map_type = 0x0001;

	ret = osd_create_partition(osd, PARTITION_PID_LB, cdb_cont_len, sense);
	assert(ret == 0);
	ret = osd_create(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB, 0, cdb_cont_len, sense);
	assert(ret == 0);

	sprintf(wrbuf, "Te\n");
	ret = osd_write(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB,
			strlen(wrbuf)+1, 0, wrbuf, cdb_cont_len, sense, DDT_CONTIG);
	assert(ret == 0);
	get_dfile_name(path, osd->root, USEROBJECT_PID_LB, USEROBJECT_OID_LB);

	/* Illegal case: offset > file_size */
	ret = osd_read_map(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB, 24, 12, map_type,
			   outdata, &used_outlen, cdb_cont_len, sense);
	assert(ret != 0);
	
	/* Illegal case: allocated_len < 24 */
	ret = osd_read_map(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB, 10, 2, map_type,
			   outdata, &used_outlen, cdb_cont_len, sense);
	assert(ret != 0);

	/* One descriptor case */
	ret = osd_read_map(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB, 24, 0, map_type,
			   outdata, &used_outlen, cdb_cont_len, sense);
	assert(ret == 0);
	printf("------------------------------------------- \n");
	printf("OUTPUTS FOR 1 DESCRIPTOR CASE \n------------------------------------------- \n");
	printf("additional length: %i \n", *(outdata+7));
	printf("map_type: %i \n", *(outdata+11));
	printf("data length: %i \n", *(outdata+15));
	printf("data byte offset: %i \n", *(outdata+23));

	/* Two descriptor case */
	sprintf(wrbuf, "Testin\n");
	ret = osd_write(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB,
			strlen(wrbuf)+1, 0, wrbuf, cdb_cont_len, sense, DDT_CONTIG);
	assert(ret == 0);
	ret = osd_read_map(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB, 1024, 0, map_type,
			   outdata, &used_outlen, cdb_cont_len, sense);
	assert(ret == 0);
	printf("------------------------------------------- \n");
	printf("OUTPUTS FOR 2 DESCRIPTORS CASE \n------------------------------------------- \n");
	printf("additional length: %i \n", *(outdata+7));
	printf("map_type(1st descriptor): %i \n", *(outdata+11));
	printf("data length(1st descriptor): %i \n", *(outdata+15));
	printf("data byte offset(1st descriptor): %i \n", *(outdata+23));
	printf("map_type(2nd descriptor): %i \n", *(outdata+27));
	printf("data length(2nd descriptor): %i \n", *(outdata+31));
	printf("data byte offset(2nd descriptor): %i \n", *(outdata+39));
	
	/* Offset > 0 */
	sprintf(wrbuf, "Testin\n");
	ret = osd_write(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB,
			strlen(wrbuf)+1, 0, wrbuf, cdb_cont_len, sense, DDT_CONTIG);
	assert(ret == 0);
	ret = osd_read_map(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB, 1024, 4, map_type,
			   outdata, &used_outlen, cdb_cont_len, sense);
	assert(ret == 0);
	printf("------------------------------------------- \n");
	printf("1 DESCRIPTOR W/ OFFSET > 0 CASE \n------------------------------------------- \n");
	printf("additional length: %i \n", *(outdata+7));
	printf("map_type: %i \n", *(outdata+11));
	printf("data length: %i \n", *(outdata+15));
	printf("data byte offset: %i \n", *(outdata+23));
	
	/* Special Case: allocated_len = 0 */
	ret = osd_read_map(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB, 0, 4, map_type,
			   outdata, &used_outlen, cdb_cont_len, sense);
	assert(ret == 0);

	free(sense);
	free(wrbuf);
	free(outdata);
}

static void test_osd_io(struct osd_device *osd)
{
	int ret = 0;
	uint8_t *sense = Calloc(1, 1024);
	void *wrbuf = Calloc(1, 256);
	void *apbuf = Calloc(1, 256);
	void *rdbuf = Calloc(1, 256);
	char *cp = 0;
	uint64_t len;
	uint32_t cdb_cont_len = 0;

	ret = osd_create_partition(osd, PARTITION_PID_LB, cdb_cont_len, sense);
	assert(ret == 0);
	ret = osd_create(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB, 0, cdb_cont_len, sense);
	assert(ret == 0);

	sprintf(wrbuf, "Hello World! Get life\n");
	ret = osd_write(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB,
			strlen(wrbuf)+1, 0, wrbuf, cdb_cont_len, sense, DDT_CONTIG);
	assert(ret == 0);

	ret = osd_read(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB,
		       256, 0, NULL, rdbuf, &len, cdb_cont_len, sense, DDT_CONTIG);
	assert(ret >= 0);
	if (ret > 0) {
		assert(sense_test_type(sense, OSD_SSK_RECOVERED_ERROR,
				       OSD_ASC_READ_PAST_END_OF_USER_OBJECT));
		assert(get_ntohll(&sense[44]) == len);
	}
	assert(len == strlen(wrbuf)+1);
	assert(strcmp(rdbuf, wrbuf) == 0);

	sprintf(apbuf, "this text is appended\n");
	ret = osd_append(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB,
			 strlen(apbuf)+1, apbuf, cdb_cont_len, sense, DDT_CONTIG);
	assert(ret == 0);

	memset(rdbuf, 0, len);
	ret = osd_read(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB,
		       256, 0, NULL, rdbuf, &len, cdb_cont_len, sense, DDT_CONTIG);
	assert(ret >= 0);
	if (ret > 0) {
		assert(sense_test_type(sense, OSD_SSK_RECOVERED_ERROR,
				       OSD_ASC_READ_PAST_END_OF_USER_OBJECT));
		assert(get_ntohll(&sense[44]) == len);
	}
	assert(len == strlen(wrbuf)+1+strlen(apbuf)+1);
	cp = rdbuf;
	assert(strncmp(cp, wrbuf, strlen(wrbuf)+1) == 0);
	cp += strlen(wrbuf)+1;
	assert(strncmp(cp, apbuf, strlen(apbuf)+1) == 0);
	cp -= strlen(wrbuf)+1;

	ret = osd_remove(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB, cdb_cont_len, sense);
	assert(ret == 0);

	ret = osd_remove_partition(osd, PARTITION_PID_LB, cdb_cont_len, sense);
	assert(ret == 0);

	free(sense);
	free(rdbuf);
	free(wrbuf);
	free(apbuf);
}

static void test_osd_create_partition(struct osd_device *osd)
{
	int ret = 0;
	void *sense = Calloc(1, 1024);
	uint32_t cdb_cont_len = 0;

	ret = osd_create_partition(osd, 0, cdb_cont_len, sense);
	assert(ret == 0);

	/* create dup pid must fail */
	ret = osd_create_partition(osd, PARTITION_PID_LB, cdb_cont_len, sense);
	assert(ret != 0);

	ret = osd_remove_partition(osd, PARTITION_PID_LB, cdb_cont_len, sense);
	assert(ret == 0);

	/* remove non-existing object, test must succeed: sqlite semantics */
	ret = osd_remove_partition(osd, PARTITION_PID_LB, cdb_cont_len, sense);
	assert(ret == 0);

	free(sense);
}

static void test_osd_get_ccap(struct osd_device *osd)
{
	int ret = 0, i = 0;
	void *sense = Calloc(1, 1024);
	void *val = Calloc(1, 1024);
	uint8_t *cp = NULL;
	uint32_t used_len = 0;
	uint32_t cdb_cont_len = 0;
	uint64_t len = 0;

	ret = osd_create_partition(osd, PARTITION_PID_LB, cdb_cont_len, sense);
	assert(ret == 0);

	ret = osd_create(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB, 0, cdb_cont_len, sense);
	assert(ret == 0);

	len = 1024;
	used_len = 0;
	ret = osd_getattr_page(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB,
			       CUR_CMD_ATTR_PG, val, len, TRUE, &used_len,
			       cdb_cont_len, sense);
	assert(ret == 0);
	assert(used_len == CCAP_TOTAL_LEN);

	cp = val;
	assert(get_ntohl(&cp[0]) == CUR_CMD_ATTR_PG);
	assert(get_ntohl(&cp[4]) == CCAP_TOTAL_LEN - 8);
	for (i = CCAP_RICV_OFF; i < CCAP_RICV_OFF + CCAP_RICV_LEN; i++)
		assert(cp[i] == 0);
	assert(cp[CCAP_OBJT_OFF] == USEROBJECT);
	assert(get_ntohll(&cp[CCAP_PID_OFF]) == USEROBJECT_PID_LB);
	assert(get_ntohll(&cp[CCAP_OID_OFF]) == USEROBJECT_OID_LB);
	assert(get_ntohll(&cp[CCAP_APPADDR_OFF]) == 0);

	ret = osd_remove(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB, cdb_cont_len, sense);
	assert(ret == 0);

	ret = osd_remove_partition(osd, PARTITION_PID_LB, cdb_cont_len, sense);
	assert(ret == 0);

	free(sense);
	free(val);
}

static inline time_t ntoh_time(void *buf)
{
	time_t t = get_ntohll(buf) & ~0xFFFFULL;
	return (t >> 16);
}

static void test_osd_get_utsap(struct osd_device *osd)
{
	int ret = 0;
	void *sense = Calloc(1, 1024);
	void *buf = Calloc(1, 1024);
	uint8_t *cp = NULL;
	uint32_t used_getlen = 0;
	uint32_t cdb_cont_len = 0;
	uint64_t used_len = 0;
	uint64_t len = 0;

	ret = osd_create_partition(osd, PARTITION_PID_LB, cdb_cont_len, sense);
	assert(ret == 0);

	ret = osd_create(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB, 0, cdb_cont_len, sense);
	assert(ret == 0);

	sprintf(buf, "Hello World! Get life blah blah blah\n");
	ret = osd_write(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB,
			strlen(buf)+1, 0, buf, cdb_cont_len, sense, DDT_CONTIG);
	assert(ret == 0);

	sleep(1);
	used_len = 0;
	ret = osd_read(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB,
		       strlen(buf)+1, 0, NULL, buf, &used_len, cdb_cont_len, sense, DDT_CONTIG);
	assert(ret == 0);
	assert(used_len = strlen(buf)+1);

	/*sleep(1);*/
	ret = osd_set_attributes(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB,
				 USEROBJECT_PG + LUN_PG_LB, 2, buf,
				 strlen(buf)+1, TRUE, cdb_cont_len, sense);
	assert(ret == 0);

	len = 1024;
	used_getlen = 0;
	ret = osd_getattr_page(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB,
			       USER_TMSTMP_PG, buf, len, FALSE, &used_getlen,
			       cdb_cont_len, sense);
	assert(ret == 0);

	cp = buf;
	assert(get_ntohl(&cp[0]) == USER_TMSTMP_PG);
	assert(get_ntohl(&cp[4]) == UTSAP_TOTAL_LEN - 8);

	time_t atime = ntoh_time(&cp[UTSAP_DATA_ATIME_OFF]);
	time_t mtime = ntoh_time(&cp[UTSAP_DATA_MTIME_OFF]);
	assert(atime != 0 && mtime != 0 && mtime < atime);

	atime = ntoh_time(&cp[UTSAP_ATTR_ATIME_OFF]);
	mtime = ntoh_time(&cp[UTSAP_ATTR_MTIME_OFF]);
	assert(atime != 0 && mtime != 0 && mtime == atime);

	ret = osd_remove(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB, cdb_cont_len, sense);
	assert(ret == 0);

	ret = osd_remove_partition(osd, PARTITION_PID_LB, cdb_cont_len, sense);
	assert(ret == 0);

	free(sense);
	free(buf);
}

static void test_osd_get_attributes(struct osd_device *osd)
{
	int ret = 0;
	uint32_t used_len = 0;
	uint32_t cdb_cont_len = 0;
	uint64_t len = 0;
	void *sense = Calloc(1, 1024);
	void *val = Calloc(1, 1024);
	void *getval = Calloc(1, 1024);
	struct list_entry *le = NULL;

	ret = osd_create_partition(osd, PARTITION_PID_LB, cdb_cont_len, sense);
	assert(ret == 0);

	ret = osd_create(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB, 0, cdb_cont_len, sense);
	assert(ret == 0);

	sprintf(val, "Madhuri Dixit");
	ret = osd_set_attributes(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB,
				 USEROBJECT_PG + LUN_PG_LB, 1, val,
				 strlen(val)+1, TRUE, cdb_cont_len, sense);
	assert(ret == 0);

	len = 1024;
	used_len = 0;
	ret = osd_getattr_list(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB,
			       USEROBJECT_PG + LUN_PG_LB, 1, val, len, TRUE,
			       RTRVD_SET_ATTR_LIST, &used_len, cdb_cont_len, sense);
	assert(ret == 0);
	le = val;
	assert(get_ntohl(&le->page) == USEROBJECT_PG + LUN_PG_LB);
	assert(get_ntohl(&le->number) == 1);
	assert(get_ntohs(&le->len) == strlen("Madhuri Dixit")+1);
	assert(strcmp((char *)le +  LE_VAL_OFF, "Madhuri Dixit") == 0);
	len = LE_VAL_OFF + strlen("Madhuri Dixit")+1;
	len += (0x8 - (len & 0x7)) & 0x7;
	assert(used_len == len);

	/* write to the file and then truncate using setting logical len */
	sprintf(val, "Hello World! Get life\n");
	ret = osd_write(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB,
			strlen(val)+1, 0, val, cdb_cont_len, sense, DDT_CONTIG);
	assert(ret == 0);

	len = 1024;
	ret = osd_getattr_list(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB,
			       USER_INFO_PG, UIAP_LOGICAL_LEN, getval, len,
			       TRUE, RTRVD_SET_ATTR_LIST, &used_len, cdb_cont_len, sense);
	assert(ret == 0);
	le = getval;
	assert(get_ntohl(&le->page) == USER_INFO_PG);
	assert(get_ntohl(&le->number) == UIAP_LOGICAL_LEN);
	assert(get_ntohs(&le->len) == UIAP_LOGICAL_LEN_LEN);
	assert(get_ntohll(le + LE_VAL_OFF) == strlen(val)+1);
	len = LE_VAL_OFF + UIAP_LOGICAL_LEN_LEN;
	len += (0x8 - (len & 0x7)) & 0x7;
	assert(used_len == len);

	set_htonll(val, 0);
	ret = osd_set_attributes(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB,
				 USER_INFO_PG, UIAP_LOGICAL_LEN, val, 8, TRUE,
				 cdb_cont_len, sense);
	assert(ret == 0);

	len = 1024;
	ret = osd_getattr_list(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB,
			       USER_INFO_PG, UIAP_LOGICAL_LEN, getval, len,
			       TRUE, RTRVD_SET_ATTR_LIST, &used_len, cdb_cont_len, sense);
	assert(ret == 0);
	le = getval;
	assert(get_ntohl(&le->page) == USER_INFO_PG);
	assert(get_ntohl(&le->number) == UIAP_LOGICAL_LEN);
	assert(get_ntohs(&le->len) == UIAP_LOGICAL_LEN_LEN);
	assert(get_ntohll(le + LE_VAL_OFF) == 0);
	len = LE_VAL_OFF + UIAP_LOGICAL_LEN_LEN;
	len += (0x8 - (len & 0x7)) & 0x7;
	assert(used_len == len);

	ret = osd_remove(osd, USEROBJECT_PID_LB, USEROBJECT_OID_LB, cdb_cont_len, sense);
	assert(ret == 0);

	ret = osd_remove_partition(osd, PARTITION_PID_LB, cdb_cont_len, sense);
	assert(ret == 0);

	free(getval);
	free(sense);
	free(val);
}

static void test_osd_create_user_tracking_collection(struct osd_device *osd)
{
        int ret = 0;
	char buffer [50];
	uint8_t list_attr = 0;
	uint64_t cid = 0;
	uint64_t oid = 0;
	uint64_t alloc_len;
	uint64_t used_outlen = 0;
      	uint32_t cdb_cont_len = 0;
	void *buf = Calloc(1, 1024);
	void *sense = Calloc(1, 1024);
	uint8_t *outdata = Calloc(1, 1024);;
	struct getattr_list get_attr;

	get_attr.sz = 0;

	if (!sense || !buf)
	        return;
	
	/* invalid pid, test must fail */
	ret = osd_create_user_tracking_collection(osd, 0, COLLECTION_OID_LB, COLLECTION_OID_LB + 1,
						  cdb_cont_len, sense);
	assert(ret != 0);
	
	/* invalid cid, non-existant pid, test must fail */
	ret = osd_create_user_tracking_collection(osd, COLLECTION_PID_LB, 1, COLLECTION_OID_LB, 
						  cdb_cont_len, sense);
	assert(ret != 0);

	/* remove non-existing collection, test must fail */
	ret = osd_remove_collection(osd, COLLECTION_PID_LB, COLLECTION_OID_LB,
				    1, cdb_cont_len, sense);
	assert(ret != 0);

	/* create a partition */
	ret = osd_create_partition(osd, PARTITION_PID_LB, cdb_cont_len, sense);
	assert(ret == 0);
	
	/* create a empty collection */
	ret = osd_create_collection(osd, COLLECTION_PID_LB, 0, cdb_cont_len, sense);
	assert(ret == 0);
	assert(osd->ccap.oid == COLLECTION_OID_LB);

	/* create 1st user object */
	ret = osd_create(osd, USEROBJECT_PID_LB, 0, 1, cdb_cont_len, sense);
	assert(ret == 0);
	assert(osd->ccap.oid == (USEROBJECT_OID_LB + 1));
	oid = osd->ccap.oid;

	/* add 1st object to COLLECTION_OID_LB */
	cid = COLLECTION_OID_LB;
	set_htonll(buf, cid);
	ret = osd_set_attributes(osd, USEROBJECT_PID_LB, oid, USER_COLL_PG,
				 1, buf, sizeof(cid), 0, cdb_cont_len, sense);
	assert(ret == 0);
		
	/* create 2nd user object */
	ret = osd_create(osd, USEROBJECT_PID_LB, 0, 1, cdb_cont_len, sense);
	assert(ret == 0);
	assert(osd->ccap.oid == (USEROBJECT_OID_LB + 2));
	oid = osd->ccap.oid;

	/* add 2nd object to COLLECTION_OID_LB */
	cid = COLLECTION_OID_LB;
	set_htonll(buf, cid);
	ret = osd_set_attributes(osd, USEROBJECT_PID_LB, oid, USER_COLL_PG,
				 1, buf, sizeof(cid), 0, cdb_cont_len, sense);
	assert(ret == 0);
	
	/* create 3rd user object */
	ret = osd_create(osd, USEROBJECT_PID_LB, 0, 1, cdb_cont_len, sense);
	assert(ret == 0);
	assert(osd->ccap.oid == (USEROBJECT_OID_LB + 3));
	oid = osd->ccap.oid;

	/* add 3rd object to COLLECTION_OID_LB */
	cid = COLLECTION_OID_LB;
	set_htonll(buf, cid);
	ret = osd_set_attributes(osd, USEROBJECT_PID_LB, oid, USER_COLL_PG,
				 1, buf, sizeof(cid), 0, cdb_cont_len, sense);
	assert(ret == 0);

	/* create user tracking collection, requested_cid = 0 */
	cdb_cont_len = 1;
	ret = osd_create_user_tracking_collection(osd, PARTITION_PID_LB, 0, COLLECTION_OID_LB,
						  cdb_cont_len, sense);
	assert(ret == 0);

	cid = osd->ccap.oid;
	/* remove collection */
	ret = osd_remove_collection(osd, PARTITION_PID_LB, cid, 1, cdb_cont_len, sense);
	assert(ret == 0);
	
	/* create user tracking collection, requested_cid != 0 */
	cdb_cont_len = 1;
	ret = osd_create_user_tracking_collection(osd, PARTITION_PID_LB, COLLECTION_OID_LB+5, COLLECTION_OID_LB,
						  cdb_cont_len, sense);
	assert(ret == 0);

	/* verify collection contents */
	alloc_len = 100;
	
	osd_list_collection(osd, list_attr, PARTITION_PID_LB,
			    COLLECTION_OID_LB+5, alloc_len, 0, &get_attr, 0,
			    outdata, &used_outlen, sense);

	sprintf(buffer, "%i \n", *(outdata+7));
	printf("addtional len is: %i \n", atoi(buffer));
	assert(atoi(buffer) == 40);

	printf("used len is: %lli \n", llu(used_outlen));
	assert(used_outlen == (3*8) + 24);
	
	oid = get_ntohll(outdata+24);
	printf("1st object id =: %llx \n", llu(oid));
	assert(oid == USEROBJECT_OID_LB + 1);

	oid = get_ntohll(outdata+32);
	printf("2nd object id =: %llx \n", llu(oid));
	assert(oid == USEROBJECT_OID_LB + 2);

	oid = get_ntohll(outdata+40);
	printf("3rd object id =: %llx \n", llu(oid));
	assert(oid == USEROBJECT_OID_LB + 3);

	/* remove objects */
	ret = osd_remove(osd, PARTITION_PID_LB, USEROBJECT_PID_LB+3, cdb_cont_len, sense);
	assert(ret == 0);

	ret = osd_remove(osd, PARTITION_PID_LB, USEROBJECT_PID_LB+2, cdb_cont_len, sense);
	assert(ret == 0);

	ret = osd_remove(osd, PARTITION_PID_LB, USEROBJECT_PID_LB+1, cdb_cont_len, sense);
	assert(ret == 0);

	/* remove collections */
	cid = COLLECTION_OID_LB;
	ret = osd_remove_collection(osd, PARTITION_PID_LB, COLLECTION_PID_LB, 1, cdb_cont_len, sense);
	assert(ret == 0);

	cid = COLLECTION_OID_LB;
	ret = osd_remove_collection(osd, PARTITION_PID_LB, COLLECTION_PID_LB + 5, 1, cdb_cont_len, sense);
	assert(ret == 0);
	
	/* remove partition */
	ret = osd_remove_partition(osd, PARTITION_PID_LB, cdb_cont_len, sense);
	assert(ret == 0);
	
	free(outdata);
	free(sense);
	free(buf);
}


static void test_osd_create_collection(struct osd_device *osd)
{
	int ret = 0;
	uint64_t cid = 0;
	uint64_t oid = 0;
	uint32_t number = 0;
	uint32_t cdb_cont_len = 0;
	void *buf = Calloc(1, 1024);
	void *sense = Calloc(1, 1024);

	if (!sense || !buf)
		return;

	/* invalid pid/cid, test must fail */
	ret = osd_create_collection(osd, 0, 1, cdb_cont_len, sense);
	assert(ret != 0);

	/* invalid cid, non-existant pid, test must fail */
	ret = osd_create_collection(osd, COLLECTION_PID_LB, 1, cdb_cont_len, sense);
	assert(ret != 0);

	ret = osd_create_partition(osd, PARTITION_PID_LB, cdb_cont_len, sense);
	assert(ret == 0);

	ret = osd_create_collection(osd, COLLECTION_PID_LB, COLLECTION_OID_LB,
				    cdb_cont_len, sense);
	assert(ret == 0);

	ret = osd_remove_collection(osd, COLLECTION_PID_LB, COLLECTION_OID_LB,
				    1, cdb_cont_len, sense);
	assert(ret == 0);

	/* remove non-existing collection, test must fail */
	ret = osd_remove_collection(osd, COLLECTION_PID_LB, COLLECTION_OID_LB,
				    1, cdb_cont_len, sense);
	assert(ret != 0);

	ret = osd_create_collection(osd, COLLECTION_PID_LB, COLLECTION_OID_LB,
				    cdb_cont_len, sense);
	assert(ret == 0);

	/* empty collection, remove will succeed even if fcr == 0 */
	ret = osd_remove_collection(osd, COLLECTION_PID_LB, COLLECTION_OID_LB,
				    0, cdb_cont_len, sense);
	assert(ret == 0);

	ret = osd_create_collection(osd, COLLECTION_PID_LB, COLLECTION_OID_LB,
				    cdb_cont_len, sense);
	assert(ret == 0);

	ret = osd_create_collection(osd, COLLECTION_PID_LB, 0, cdb_cont_len, sense);
	assert(ret == 0);
	assert(osd->ccap.oid == COLLECTION_OID_LB + 1);

	/* create objects */
	ret = osd_create(osd, USEROBJECT_PID_LB, 0, 10, cdb_cont_len, sense);
	assert(ret == 0);
	assert(osd->ccap.oid == (USEROBJECT_OID_LB + 12 - 1));

	ret = osd_remove_collection(osd, COLLECTION_PID_LB, COLLECTION_OID_LB,
				    1, cdb_cont_len, sense);
	assert(ret == 0);

	/* add objects to collection */
	cid = COLLECTION_OID_LB + 1;
	set_htonll(buf, cid);
	number = 1;
	for (oid = USEROBJECT_OID_LB+2; oid < (USEROBJECT_OID_LB+9); oid++) {
		ret = osd_set_attributes(osd, USEROBJECT_PID_LB, oid,
					 USER_COLL_PG, number, buf,
					 sizeof(cid), 0, cdb_cont_len, sense);
		assert(ret == 0);
		number++;
	}
	/* remove collection */
	ret = osd_remove_collection(osd, COLLECTION_PID_LB,
				    COLLECTION_OID_LB + 1, 1, cdb_cont_len, sense);
	assert(ret == 0);

	/* remove objects */
	for (oid = USEROBJECT_OID_LB+2; oid < (USEROBJECT_OID_LB+12); oid++) {
	        ret = osd_remove(osd, USEROBJECT_PID_LB, oid, cdb_cont_len, sense);
		assert(ret == 0);
	}

	/* create 3 collections */
	ret = osd_create_collection(osd, COLLECTION_PID_LB, 0, cdb_cont_len, sense);
	assert(ret == 0);
	assert(osd->ccap.oid == COLLECTION_OID_LB);

	ret = osd_create_collection(osd, COLLECTION_PID_LB, 0, cdb_cont_len, sense);
	assert(ret == 0);
	assert(osd->ccap.oid == COLLECTION_OID_LB + 1);

	ret = osd_create_collection(osd, COLLECTION_PID_LB, 0, cdb_cont_len, sense);
	assert(ret == 0);
	assert(osd->ccap.oid == COLLECTION_OID_LB + 2);

	/* create object */
	ret = osd_create(osd, USEROBJECT_PID_LB, 0, 1, cdb_cont_len, sense);
	assert(ret == 0);
	assert(osd->ccap.oid == (USEROBJECT_OID_LB + 3));
	oid = osd->ccap.oid;

	/* add object to COLLECTION_OID_LB */
	cid = COLLECTION_OID_LB;
	set_htonll(buf, cid);
	ret = osd_set_attributes(osd, USEROBJECT_PID_LB, oid, USER_COLL_PG,
				 1, buf, sizeof(cid), 0, cdb_cont_len, sense);
	assert(ret == 0);
	
	/* add object to COLLECTION_OID_LB+1 */
	cid = COLLECTION_OID_LB + 1;
	set_htonll(buf, cid);
	ret = osd_set_attributes(osd, USEROBJECT_PID_LB, oid, USER_COLL_PG,
				 2, buf, sizeof(cid), 0, cdb_cont_len, sense);
	assert(ret == 0);

	/* 
	 * add object to COLLECTION_OID_LB+2 and 
	 * remove it from COLLECTION_OID_LB+1 
	 */
	cid = COLLECTION_OID_LB + 2;
	set_htonll(buf, cid);
	ret = osd_set_attributes(osd, USEROBJECT_PID_LB, oid, USER_COLL_PG,
				 2, buf, sizeof(cid), 0, cdb_cont_len, sense);
	assert(ret == 0);

	/* remove collections */
	cid = COLLECTION_OID_LB;
	ret = osd_remove_collection(osd, COLLECTION_PID_LB, cid, 1, cdb_cont_len, sense);
	assert(ret == 0);
	cid = COLLECTION_OID_LB+1;
	ret = osd_remove_collection(osd, COLLECTION_PID_LB, cid, 1, cdb_cont_len, sense);
	assert(ret == 0);
	cid = COLLECTION_OID_LB+2;
	ret = osd_remove_collection(osd, COLLECTION_PID_LB, cid, 1, cdb_cont_len, sense);
	assert(ret == 0);

	/* remove object */
	ret = osd_remove(osd, USEROBJECT_PID_LB, oid, cdb_cont_len, sense);
	assert(ret == 0);

	ret = osd_remove_partition(osd, PARTITION_PID_LB, cdb_cont_len, sense);
	assert(ret == 0);

	free(sense);
	free(buf);
}


/* only to be used by test_osd_query */
static inline void set_attr_int(struct osd_device *osd, uint64_t oid,
				uint32_t page, uint32_t number, uint64_t val,
				uint8_t *sense)
{
	int ret = 0;
	uint64_t pid = PARTITION_PID_LB;
	uint32_t cdb_cont_len = 0;

	set_htonll(&val, val);
	ret = osd_set_attributes(osd, pid, oid, page, number, &val,
				 sizeof(val), 1, cdb_cont_len, sense);
	assert(ret == 0);
}

static inline void set_attr_val(struct osd_device *osd, uint64_t oid,
				uint32_t page, uint32_t number,
				const void *val, uint16_t len,
				uint8_t *sense)
{
	int ret = 0;
	uint64_t pid = PARTITION_PID_LB;
	uint32_t cdb_cont_len = 0;

	ret = osd_set_attributes(osd, pid, oid, page, number, val, len, 1,
				 cdb_cont_len, sense);
	assert(ret == 0);
}

static inline void set_qce(uint8_t *cp, uint32_t page, uint32_t number,
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

static inline int ismember(uint64_t needle, uint64_t *hay, uint64_t haysz)
{
	/* osd_debug("%s: %016llx", __func__, llu(needle)); */
	while (haysz--) {
		if (needle == hay[haysz])
			return TRUE;
	}
	return FALSE;
}

static void check_results(void *ml, uint64_t *idlist, uint64_t sz,
			  uint64_t usedlen)
{
	uint8_t *cp = ml;
	uint32_t add_len = get_ntohll(&cp[0]);
	assert(add_len == (5+8*sz));
	assert(cp[12] == (0x21 << 2));
	assert(usedlen == add_len+8);
	add_len -= 5;
	cp += MIN_ML_LEN;
	while (add_len) {
		assert(ismember(get_ntohll(cp), idlist, 8));
		cp += 8;
		add_len -= 8;
	}
	memset(idlist, 0, sz*sizeof(*idlist));
}

static void test_osd_query(struct osd_device *osd)
{
	int ret = 0;
	uint64_t cid = 0;
	uint64_t oid = 0;
	uint64_t pid = 0;
	uint8_t *cp = NULL;
	uint64_t min = 0, max = 0;
	uint64_t usedlen = 0;
	uint32_t page = USER_COLL_PG;
	uint32_t qll = 0;
	uint32_t cdb_cont_len = 0;
	void *buf = Calloc(1, 1024);
	void *sense = Calloc(1, 1024);
	void *matcheslist = Calloc(1, 4096);
	uint64_t *idlist = Calloc(sizeof(*idlist), 512);

	if (!sense || !buf || !matcheslist || !idlist)
		goto out;

	pid = PARTITION_PID_LB;
	ret = osd_create_partition(osd, pid, cdb_cont_len, sense);
	assert(ret == 0);

	/* create objects */
	ret = osd_create(osd, pid, 0, 10, cdb_cont_len, sense);
	assert(ret == 0);

	ret = osd_create_collection(osd, pid, 0, cdb_cont_len, sense);
	assert(ret == 0);
	cid = COLLECTION_OID_LB + 10;
	assert(osd->ccap.oid == cid);

	/* set attributes */
	oid = USEROBJECT_OID_LB;
	page = USEROBJECT_PG + LUN_PG_LB;
	set_attr_int(osd, oid,   page, 1, 4, sense);
	set_attr_int(osd, oid+1, page, 1, 49, sense);
	set_attr_int(osd, oid+1, page, 2, 130, sense);
	set_attr_int(osd, oid+2, page, 1, 20, sense);
	set_attr_int(osd, oid+3, page, 1, 101, sense);
	set_attr_int(osd, oid+4, page, 1, 59, sense);
	set_attr_int(osd, oid+4, page, 2, 37, sense);
	set_attr_int(osd, oid+5, page, 1, 75, sense);
	set_attr_int(osd, oid+6, page, 1, 200, sense);
	set_attr_int(osd, oid+7, page, 1, 67, sense);
	set_attr_int(osd, oid+8, page, 1, 323, sense);
	set_attr_int(osd, oid+8, page, 2, 44, sense);
	set_attr_int(osd, oid+9, page, 1, 1, sense);
	set_attr_int(osd, oid+9, page, 2, 19, sense);

	/* include some objects in the collection */
	page = USER_COLL_PG;
	set_attr_int(osd, oid,   page, 1, cid, sense);
	set_attr_int(osd, oid+1, page, 1, cid, sense);
	set_attr_int(osd, oid+3, page, 1, cid, sense);
	set_attr_int(osd, oid+4, page, 1, cid, sense);
	set_attr_int(osd, oid+5, page, 1, cid, sense);
	set_attr_int(osd, oid+6, page, 1, cid, sense);
	set_attr_int(osd, oid+7, page, 1, cid, sense);
	set_attr_int(osd, oid+9, page, 1, cid, sense);

	/* 1: run without query criteria */
	qll = MINQLISTLEN;
	memset(buf, 0, 1024);

	ret = osd_query(osd, pid, cid, qll, 4096, buf, matcheslist, &usedlen,
			cdb_cont_len, sense);
	assert(ret == 0);

	idlist[0] = oid;
	idlist[1] = oid+1;
	idlist[2] = oid+3;
	idlist[3] = oid+4;
	idlist[4] = oid+5;
	idlist[5] = oid+6;
	idlist[6] = oid+7;
	idlist[7] = oid+9;
	check_results(matcheslist, idlist, 8, usedlen);

	/* 2: run one query without min/max constraints */
	qll = 0;
	memset(buf, 0, 1024);
	cp = buf;
	cp[0] = 0x0;
	page = USEROBJECT_PG+LUN_PG_LB;
	set_qce(&cp[4], page, 2, 0, NULL, 0, NULL);
	qll += 4 + (4+4+4+2+2);

	ret = osd_query(osd, pid, cid, qll, 4096, buf, matcheslist, &usedlen,
			cdb_cont_len, sense);
	assert(ret == 0);

	idlist[0] = oid+1;
	idlist[1] = oid+4;
	idlist[2] = oid+9;
	check_results(matcheslist, idlist, 3, usedlen);

	/* 3: run one query with criteria */
	qll = 0;
	min = 40, max= 80;
	set_htonll(&min, min);
	set_htonll(&max, max);
	memset(buf, 0, 1024);
	cp = buf;
	cp[0] = 0x0;
	page = USEROBJECT_PG+LUN_PG_LB;
	set_qce(&cp[4], page, 1, sizeof(min), &min, sizeof(max), &max);
	qll += 4 + (4+4+4+2+sizeof(min)+2+sizeof(max));

	ret = osd_query(osd, pid, cid, qll, 4096, buf, matcheslist, &usedlen,
			cdb_cont_len, sense);
	assert(ret == 0);

	idlist[0] = oid+1;
	idlist[1] = oid+4;
	idlist[2] = oid+5;
	idlist[3] = oid+7;
	check_results(matcheslist, idlist, 4, usedlen);

	/* 4: run union of two query criteria */
	qll = 0;

	/* first query */
	page = USEROBJECT_PG+LUN_PG_LB;
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

	ret = osd_query(osd, pid, cid, qll, 4096, buf, matcheslist, &usedlen,
			cdb_cont_len, sense);
	assert(ret == 0);
	idlist[0] = oid+3;
	idlist[1] = oid+6;
	check_results(matcheslist, idlist, 2, usedlen);

	/* 5: run intersection of 2 query criteria */
	qll = 0;

	/* first query */
	page = USEROBJECT_PG+LUN_PG_LB;
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

	ret = osd_query(osd, pid, cid, qll, 4096, buf, matcheslist, &usedlen,
			cdb_cont_len, sense);
	assert(ret == 0);

	idlist[0] = oid+1;
	idlist[1] = oid+4;
	check_results(matcheslist, idlist, 2, usedlen);

	/* 6: run union of 3 query criteria, with missing min/max */
	qll = 0;

	/* first query */
	min = 130, max = 130;
	set_htonll(&min, min);
	set_htonll(&max, max);
	memset(buf, 0, 1024);
	cp = buf;
	cp[0] = 0x0; /* UNION */
	page = USEROBJECT_PG+LUN_PG_LB;
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

	ret = osd_query(osd, pid, cid, qll, 4096, buf, matcheslist, &usedlen,
			cdb_cont_len, sense);
	assert(ret == 0);
	idlist[3] = oid;
	idlist[4] = oid+1;
	idlist[5] = oid+6;
	idlist[2] = oid+9;
	check_results(matcheslist, idlist, 4, usedlen);

	/* set some attributes with text values */
	oid = USEROBJECT_OID_LB;
	page = USEROBJECT_PG + LUN_PG_LB;
	set_attr_val(osd, oid,   page, 1, "hello", 6, sense);
	set_attr_val(osd, oid+1, page, 1, "cat", 4, sense);
	set_attr_int(osd, oid+1, page, 2, 130, sense);
	set_attr_int(osd, oid+2, page, 1, 20, sense);
	set_attr_val(osd, oid+3, page, 1, "zebra", 6, sense);
	set_attr_int(osd, oid+4, page, 1, 59, sense);
	set_attr_int(osd, oid+4, page, 2, 37, sense);
	set_attr_int(osd, oid+5, page, 1, 75, sense);
	set_attr_val(osd, oid+6, page, 1, "keema", 6, sense);
	set_attr_int(osd, oid+7, page, 1, 67, sense);
	set_attr_int(osd, oid+8, page, 1, 323, sense);
	set_attr_int(osd, oid+8, page, 2, 44, sense);
	set_attr_int(osd, oid+9, page, 1, 1, sense);
	set_attr_val(osd, oid+9, page, 2, "hotelling", 10, sense);

	/* 7: run queries on different datatypes, with diff min max lengths */
	qll = 0;

	/* first query */
	min = 41, max = 169;
	set_htonll(&min, min);
	set_htonll(&max, max);
	memset(buf, 0, 1024);
	cp = buf;
	cp[0] = 0x0; /* UNION */
	page = USEROBJECT_PG+LUN_PG_LB;
	set_qce(&cp[4], page, 1, sizeof(min), &min, sizeof(max), &max);
	qll += 4 + (4+4+4+2+sizeof(min)+2+sizeof(max));
	cp += 4 + (4+4+4+2+sizeof(min)+2+sizeof(max));

	/* second query */
	set_qce(cp, page, 1, 3, "ab", 5, "keta");
	qll += (4+4+4+2+2+2+5);
	cp += (4+4+4+2+2+2+5);

	ret = osd_query(osd, pid, cid, qll, 4096, buf, matcheslist, &usedlen,
			cdb_cont_len, sense);
	assert(ret == 0);
	idlist[3] = oid;
	idlist[4] = oid+1;
	idlist[0] = oid+4;
	idlist[1] = oid+5;
	idlist[5] = oid+6;
	idlist[2] = oid+7;
	check_results(matcheslist, idlist, 6, usedlen);

	/* 8: run intersection of 3 query criteria, with missing min/max */
	qll = 0;

	/* first query */
	memset(buf, 0, 1024);
	cp = buf;
	cp[0] = 0x1; /* INTERSECTION */
	page = USEROBJECT_PG+LUN_PG_LB;
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

	ret = osd_query(osd, pid, cid, qll, 4096, buf, matcheslist, &usedlen,
			cdb_cont_len, sense);
	assert(ret == 0);
	idlist[0] = oid+1;
	check_results(matcheslist, idlist, 1, usedlen);

	/* 9: run intersection of 2 query criteria with empty result */
	qll = 0;

	/* first query */
	memset(buf, 0, 1024);
	cp = buf;
	cp[0] = 0x1; /* INTERSECTION */
	page = USEROBJECT_PG+LUN_PG_LB;
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

	ret = osd_query(osd, pid, cid, qll, 4096, buf, matcheslist, &usedlen,
			cdb_cont_len, sense);
	assert(ret == 0);
	check_results(matcheslist, idlist, 0, usedlen);

	/* remove collection */
	ret = osd_remove_collection(osd, pid, cid, 1, cdb_cont_len, sense);
	assert(ret == 0);

	/* remove objects */
	for (oid = USEROBJECT_OID_LB; oid < (USEROBJECT_OID_LB+10); oid++) {
	        ret = osd_remove(osd, USEROBJECT_PID_LB, oid, cdb_cont_len, sense);
		assert(ret == 0);
	}

	/* remove partition */
	ret = osd_remove_partition(osd, PARTITION_PID_LB, cdb_cont_len, sense);
	assert(ret == 0);

out:
	free(buf);
	free(sense);
	free(matcheslist);
	free(idlist);
}

int main()
{
	int ret = 0;
	const char *root = "/tmp/osd/";
	struct osd_device osd;

        system("rm -rf /tmp/osd");
	ret = osd_open(root, &osd);
	assert(ret == 0);
	
	test_osd_clear(&osd);
	test_osd_punch(&osd);
	test_osd_flush(&osd);
	test_osd_format(&osd);
	test_osd_create(&osd);
	test_osd_set_attributes(&osd);
	test_osd_io(&osd);
	test_osd_create_partition(&osd);
	test_osd_get_attributes(&osd);
	test_osd_get_ccap(&osd);
	test_osd_get_utsap(&osd);
	test_osd_create_collection(&osd);
	test_osd_create_user_tracking_collection(&osd);
	test_osd_query(&osd);
	test_osd_read_map(&osd);

	ret = osd_close(&osd);
	assert(ret == 0);

	return 0;
}

