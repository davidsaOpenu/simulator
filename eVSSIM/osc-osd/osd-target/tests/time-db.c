/*
 * DB timing tests.
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

#include "osd-types.h"
#include "osd.h"
#include "db.h"
#include "coll.h"
#include "obj.h"
#include "attr.h"
#include "osd-util/osd-util.h"

static void time_coll_insert(struct osd_device *osd, int numobj, int numiter, 
			     int testone)
{
	int ret = 0;
	int i = 0, j = 0;
	uint64_t start, end;
	double *t = 0;
	double mu, sd;

	t = Malloc(sizeof(*t) * numiter);
	if (!t)
		return;

	ret = coll_insert(osd->dbc, 1, 1, 2, 1);
	assert(ret == 0);
	ret = coll_delete(osd->dbc, 1, 1, 2);
	assert(ret == 0);

	if (testone == 0) {
		for (i = 0; i < numiter; i++) {
			t[i] = 0.0;
			for (j = 0; j < numobj; j++) {
				rdtsc(start);
				ret = coll_insert(osd->dbc, 1, 1, j, 1);
				rdtsc(end);
				assert(ret == 0);

				t[i] += (double)(end - start) / mhz;
			}

			ret = coll_delete_cid(osd->dbc, 1, 1);
			assert (ret == 0);
		}
	} else if (testone == 1) {
		for (i = 0; i < numobj; i++) {
			ret = coll_insert(osd->dbc, 1, 1, i, 1);
			assert (ret == 0);
		}
		
		for (i = 0; i < numiter; i++) {
			rdtsc(start);
			ret = coll_insert(osd->dbc, 1, 2, 1, 1);
			rdtsc(end);
			assert(ret == 0);

			ret = coll_delete(osd->dbc, 1, 2, 1);
			assert(ret == 0);

			t[i] = (double)(end - start) / mhz;
		}

		ret = coll_delete_cid(osd->dbc, 1, 1);
		assert (ret == 0);
	}

	mu = mean(t, numiter);
	sd = stddev(t, mu, numiter);
	printf("%s numiter %d numobj %d testone %d avg %lf +- %lf us\n", 
	       __func__, numiter, numobj, testone, mu, sd);
	free(t);
}

static void time_coll_delete(struct osd_device *osd, int numobj, int numiter, 
			     int testone)
{
	int ret = 0;
	int i = 0, j = 0;
	uint64_t start, end;
	double *t = 0;
	double mu, sd;

	t = Malloc(sizeof(*t) * numiter);
	if (!t)
		return;

	ret = coll_insert(osd->dbc, 1, 1, 2, 1);
	assert(ret == 0);
	ret = coll_delete(osd->dbc, 1, 1, 2);
	assert(ret == 0);

	if (testone == 0) {
		for (i = 0; i < numiter; i++) {
			for (j = 0; j < numobj; j++) {
				ret = coll_insert(osd->dbc, 1, 1, j, 1);
				assert(ret == 0);
			}

			t[i] = 0;
			for (j = 0; j < numobj; j++) {
				rdtsc(start);
				ret = coll_delete(osd->dbc, 1, 1, j);
				rdtsc(end);
				assert(ret == 0);

				t[i] += (double)(end - start) / mhz;
			}

		}
	} else if (testone == 1) {
		for (i = 0; i < numobj; i++) {
			ret = coll_insert(osd->dbc, 1, 1, i, 1);
			assert (ret == 0);
		}
		
		for (i = 0; i < numiter; i++) {
			ret = coll_insert(osd->dbc, 1, 2, 1, 1);
			assert(ret == 0);

			rdtsc(start);
			ret = coll_delete(osd->dbc, 1, 2, 1);
			rdtsc(end);
			assert(ret == 0);

			t[i] = (double)(end - start) / mhz;
		}

		ret = coll_delete_cid(osd->dbc, 1, 1);
		assert (ret == 0);
	}

	mu = mean(t, numiter);
	sd = stddev(t, mu, numiter);
	printf("%s numiter %d numobj %d testone %d avg %lf +- %lf us\n", 
	       __func__, numiter, numobj, testone, mu, sd);
	free(t);
}


static void time_obj_insert(struct osd_device *osd, int numobj, int numiter, 
			    int testone)
{
	int ret = 0;
	int i = 0, j = 0;
	uint64_t start, end;
	double *t = 0;
	double mu, sd;

	t = Malloc(sizeof(*t) * numiter);
	if (!t)
		return;

	ret = obj_insert(osd->dbc, 1, 2, 128);
	assert(ret == 0);
	ret = obj_delete(osd->dbc, 1, 2);
	assert(ret == 0);

	if (testone == 0) {
		for (i = 0; i < numiter; i++) {
			t[i] = 0.0;
			for (j = 0; j < numobj; j++) {
				rdtsc(start);
				ret = obj_insert(osd->dbc, 1, j, 128);
				rdtsc(end);
				assert(ret == 0);

				t[i] += (double)(end - start) / mhz;
			}

			ret = obj_delete_pid(osd->dbc, 1);
			assert(ret == 0);
		}
	} else if (testone == 1) {
		for (i = 0; i < numobj; i++) {
			ret = obj_insert(osd->dbc, 1, i, 1);
			assert(ret == 0);
		}
		
		for (i = 0; i < numiter; i++) {
			rdtsc(start);
			ret = obj_insert(osd->dbc, 1, numobj, 128);
			rdtsc(end);
			assert(ret == 0);

			ret = obj_delete(osd->dbc, 1, numobj);
			assert(ret == 0);

			t[i] = (double)(end - start) / mhz;
		}

		ret = obj_delete_pid(osd->dbc, 1);
		assert(ret == 0);
	}

	mu = mean(t, numiter);
	sd = stddev(t, mu, numiter);
	printf("%s numiter %d numobj %d testone %d avg %lf +- %lf us\n", 
	       __func__, numiter, numobj, testone, mu, sd);
	free(t);
}


static void time_obj_delete(struct osd_device *osd, int numobj, int numiter, 
			    int testone)
{
	int ret = 0;
	int i = 0, j = 0;
	uint64_t start, end;
	double *t = 0;
	double mu, sd;

	t = Malloc(sizeof(*t) * numiter);
	if (!t)
		return;

	ret = obj_insert(osd->dbc, 1, 2, 128);
	assert(ret == 0);
	ret = obj_delete(osd->dbc, 1, 2);
	assert(ret == 0);

	if (testone == 0) {
		for (i = 0; i < numiter; i++) {
			for (j = 0; j < numobj; j++) {
				ret = obj_insert(osd->dbc, 1, j, 128);
				assert(ret == 0);
			}

			t[i] = 0;
			for (j = 0; j < numobj; j++) {
				rdtsc(start);
				ret = obj_delete(osd->dbc, 1, j);
				rdtsc(end);
				assert(ret == 0);

				t[i] += (double)(end - start) / mhz;
			}

		}
	} else if (testone == 1) {
		for (i = 0; i < numobj; i++) {
			ret = obj_insert(osd->dbc, 1, i, 128);
			assert (ret == 0);
		}
		
		for (i = 0; i < numiter; i++) {
			ret = obj_insert(osd->dbc, 1, numobj, 128);
			assert(ret == 0);

			rdtsc(start);
			ret = obj_delete(osd->dbc, 1, numobj);
			rdtsc(end);
			assert(ret == 0);

			t[i] = (double)(end - start) / mhz;
		}

		ret = obj_delete_pid(osd->dbc, 1);
		assert (ret == 0);
	}

	mu = mean(t, numiter);
	sd = stddev(t, mu, numiter);
	printf("%s numiter %d numobj %d testone %d avg %lf +- %lf us\n", 
	       __func__, numiter, numobj, testone, mu, sd);
	free(t);
}


static void time_obj_delpid(struct osd_device *osd, int numobj, int numiter)
{
	int ret = 0;
	int i = 0, j = 0;
	uint64_t start, end;
	double *t = 0;
	double mu, sd;

	t = Malloc(sizeof(*t) * numiter);
	if (!t)
		return;

	ret = obj_insert(osd->dbc, 1, 2, 128);
	assert(ret == 0);
	ret = obj_delete_pid(osd->dbc, 1);
	assert(ret == 0);

	for (i = 0; i < numiter; i++) {
		for (j = 0; j < numobj; j++) {
			ret = obj_insert(osd->dbc, 1, j, 128);
			assert(ret == 0);
		}

		rdtsc(start);
		ret = obj_delete_pid(osd->dbc, 1);
		rdtsc(end);
		assert(ret == 0);

		t[i] = (double) (end - start) / mhz;
		start = end = 0;
	}

	mu = mean(t, numiter);
	sd = stddev(t, mu, numiter);
	printf("%s numiter %d numobj %d avg %lf +- %lf us\n", __func__,
	       numiter, numobj, mu, sd); 
	free(t);
}

/*
 * test == 1: get next pid
 * test == 2: get next oid
 * test == 3: check existing obj
 * test == 4: check non existing obj
 * test == 5: check if pid empty
 * test == 6: get type
 */
static void time_obj_generic(struct osd_device *osd, int numobj, int numiter,
			     int test)
{
	int ret = 0;
	int i = 0, j = 0;
	uint64_t start, end;
	uint64_t oid = 0, pid = 0;
	int present = 0;
	int isempty = 0;
	uint8_t obj_type = ILLEGAL_OBJ;
	double *t = 0;
	double mu, sd;
	const char *func = NULL;

	if (test < 1 || test > 6)
		return;

	t = Malloc(sizeof(*t) * numiter);
	if (!t)
		return;

	/* run a pilot */
	switch (test) {
	case 1: {
		ret = obj_insert(osd->dbc, 20, 0, 2);
		assert(ret == 0);
		pid = 0;
		ret = obj_get_nextpid(osd->dbc, &pid);
		assert(ret == 0);
		assert(pid == 21);
		ret = obj_delete(osd->dbc, 20, 0);
		assert(ret == 0);
		func = "get_next_pid";
		break;
	}
	case 2: {
		ret = obj_insert(osd->dbc, 1, 2, 128);
		assert(ret == 0);
		oid = 0;
		ret = obj_get_nextoid(osd->dbc, 1, &oid);
		assert(ret == 0);
		assert(oid == 3);
		ret = obj_delete(osd->dbc, 1, 2);
		assert(ret == 0);
		func = "get_next_oid";
		break;
	}
	case 3: 
	case 4: {
		ret = obj_insert(osd->dbc, 1, 2, 128);
		assert(ret == 0);
		ret = obj_ispresent(osd->dbc, 1, 2, &present);
		assert(ret == 0 && present == 1);
		ret = obj_delete(osd->dbc, 1, 2);
		assert(ret == 0);
		ret = obj_ispresent(osd->dbc, 1, 2, &present);
		assert(ret == 0 && present == 0);
		if (test == 3)
			func = "objpresent";
		else
			func = "objnotpresent";
		break;
	}
	case 5: {
		ret = obj_insert(osd->dbc, 1, 2, 128);
		assert(ret == 0);
		ret = obj_isempty_pid(osd->dbc, 1, &isempty);
		assert(ret == 0 && isempty == 0);
		ret = obj_delete(osd->dbc, 1, 2);
		assert(ret == 0);
		ret = obj_isempty_pid(osd->dbc, 1, &isempty);
		assert(ret == 0 && isempty == 1);
		func = "objemptypid";
		break;
	}
	case 6: {
		ret = obj_insert(osd->dbc, 1, 2, USEROBJECT);
		assert(ret == 0);
		ret = obj_get_type(osd->dbc, 1, 2, &obj_type);
		assert(ret == 0 && obj_type == USEROBJECT);
		ret = obj_delete(osd->dbc, 1, 2);
		assert(ret == 0);
		func = "objgettype";
		break;
	}
	default:
		fprintf(stderr, "1 <= test <= 6\n");
		exit(1);
	}

	for (i = 0; i < numiter; i++) {
		for (j = 1; j < numobj+1; j++) {
			if (test == 1) {
				ret = obj_insert(osd->dbc, j, 0, PARTITION);
			} else {
				ret = obj_insert(osd->dbc, 1, j, USEROBJECT);
			}
			assert(ret == 0);
		}

		switch (test) {
		case 1: {
			pid = 0;
			rdtsc(start);
			ret = obj_get_nextpid(osd->dbc, &pid);
			rdtsc(end);
			assert(ret == 0);
			assert(pid == (uint64_t)(numobj+1));
			break;
		}
		case 2: {
			oid = 0;
			rdtsc(start);
			ret = obj_get_nextoid(osd->dbc, 1, &oid);
			rdtsc(end);
			assert(ret == 0);
			assert(oid == (uint64_t)(numobj+1));
			break;
		}
		case 3: {
			oid = numobj;
			rdtsc(start);
			ret = obj_ispresent(osd->dbc, 1, oid, &present);
			rdtsc(end);
			assert(ret == 0 && present == 1);
			break;
		}
		case 4: {
			oid = numobj+2;
			rdtsc(start);
			ret = obj_ispresent(osd->dbc, 1, oid, &present);
			rdtsc(end);
			assert(ret == 0 && present == 0);
			break;
		}
		case 5: {
			rdtsc(start);
			ret = obj_isempty_pid(osd->dbc, 1, &isempty);
			rdtsc(end);
			assert(ret == 0 && isempty == 0);
			break;
		}
		case 6: {
			oid = numobj;
			rdtsc(start);
			ret = obj_get_type(osd->dbc, 1, oid, &obj_type);
			rdtsc(end);
			assert(ret == 0 && obj_type == USEROBJECT);
			break;
		}
		default:
			fprintf(stderr, "1 <= test <= 6\n");
			exit(1);
		}

		t[i] = (double) (end - start) / mhz;
		start = end = 0;

		switch (test) {
		case 1: {
			for (j = 1; j < numobj+1; j++) {
				ret = obj_delete(osd->dbc, j, 0);
				assert(ret == 0);
			}
			break;
		}
		case 2:
		case 3:
		case 4:
		case 5:
		case 6: {
			ret = obj_delete_pid(osd->dbc, 1);
			assert(ret == 0);
			break;
		}
		default:
			fprintf(stderr, "1 <= test <= 6\n");
			exit(1);
		}
	}

	mu = mean(t, numiter);
	sd = stddev(t, mu, numiter);
	printf("%s numiter %d numobj %d test, %d avg %lf +- %lf us\n", func,
	       numiter, numobj, test, mu, sd); 
	free(t);
}

/*
 * test == 1: fetch oids
 * test == 2: fetch cids
 * test == 3: fetch pids
 * test == 4: fetch pids when each pid has some objs
 */
static void time_obj_fetch(struct osd_device *osd, int numobj, int numiter, 
			   int test)
{
	int ret = 0;
	int i = 0, j = 0, k = 0;
	uint64_t start, end;
	uint64_t *ids = NULL;
	uint64_t usedlen = 0, addlen = 0, contid = 0;
	int numpid = 0;
	uint8_t *cp = 0;
	double *t = 0;
	double mu, sd;
	const char *func = NULL;

	if (test < 1 || test > 4)
		return;

	t = Calloc(numiter, sizeof(*t));
	ids = Calloc(numobj, sizeof(*ids));
	if (!t || !ids)
		return;
	cp = (uint8_t *)ids;

	/* run pilot tests */
	switch (test) {
	case 1: {
		ret = obj_insert(osd->dbc, 20, 11, 128);
		assert(ret == 0);
		ret = obj_get_oids_in_pid(osd->dbc, 20, 0, sizeof(*ids)*1,
					  cp, &usedlen, &addlen, &contid);
		assert(ret == 0);
		assert(get_ntohll(cp) == 11);
		assert(usedlen == 8), usedlen = 0;
		assert(addlen == 8), addlen = 0;
		assert(contid == 0);
		ids[0] = 0;
		ret = obj_delete_pid(osd->dbc, 20);
		assert(ret == 0);
		func = "getoids";
		break;
	}
	case 2: {
		ret = obj_insert(osd->dbc, 20, 11, 64);
		assert(ret == 0);
		ret = obj_get_cids_in_pid(osd->dbc, 20, 0, sizeof(*ids)*1,
					  cp, &usedlen, &addlen, &contid);
		assert(ret == 0);
		assert(get_ntohll(cp) == 11);
		assert(usedlen == 8), usedlen = 0;
		assert(addlen == 8), addlen = 0;
		assert(contid == 0);
		ids[0] = 0;
		ret = obj_delete_pid(osd->dbc, 20);
		assert(ret == 0);
		func = "getcids";
		break;
	}
	case 3: 
	case 4: {
		ret = obj_insert(osd->dbc, 20, 0, 2);
		assert(ret == 0);
		ret = obj_insert(osd->dbc, 10, 0, 2);
		assert(ret == 0);
		ret = obj_get_all_pids(osd->dbc, 0, sizeof(*ids)*2, cp,
				       &usedlen, &addlen, &contid);
		assert(ret == 0);
		assert(get_ntohll(cp) == 20 || get_ntohll(cp) == 10);
		assert(get_ntohll(cp+8) == 20 || get_ntohll(cp+8) == 10);
		assert(usedlen == 2*sizeof(*ids));
		assert(addlen == usedlen);
		assert(contid == 0);
		ids[0] = ids[1] = 0;
		ret = obj_delete_pid(osd->dbc, 20);
		assert(ret == 0);
		ret = obj_delete_pid(osd->dbc, 10);
		assert(ret == 0);
		if (test == 3)
			func = "getpids";
		else
			func = "getfullpids";
		break;
	}
	default:
		fprintf(stderr, "1 <= test <= 4\n");
		exit(1);
	}

	for (i = 0; i < numiter; i++) {
		switch (test) {
		case 1: 
		case 2:
			{
			for (j = 0; j < numobj; j++) {
				if (test == 1) {
					ret = obj_insert(osd->dbc, 1, j, 
							 USEROBJECT);
				} else {
					ret = obj_insert(osd->dbc, 1, j, 
							 COLLECTION);
				}
				assert(ret == 0);
			}
			cp = (uint8_t *)ids;
			usedlen = addlen = contid = 0;
			if (test == 1) {
				rdtsc(start);
				ret = obj_get_oids_in_pid(osd->dbc, 1, 0, 
							  numobj*sizeof(*ids),
							  cp, &usedlen, 
							  &addlen, &contid);
				rdtsc(end);
			} else {
				rdtsc(start);
				ret = obj_get_cids_in_pid(osd->dbc, 1, 0, 
							  numobj*sizeof(*ids),
							  cp, &usedlen, 
							  &addlen, &contid);
				rdtsc(end);
			}
			assert(usedlen == numobj * sizeof(*ids));
			assert(addlen == usedlen);
			assert(contid == 0);
			ret = obj_delete_pid(osd->dbc, 1);
			assert(ret == 0);
			break;
			}
		case 3: 
			for (j = 0; j < numobj; j++) {
				ret = obj_insert(osd->dbc, j+1, 0, 
						 PARTITION);
				assert(ret == 0);
			}
			cp = (uint8_t *)ids;
			usedlen = addlen = contid = 0;
			rdtsc(start);
			ret = obj_get_all_pids(osd->dbc, 0, 
					       sizeof(*ids)*numobj, cp,
					       &usedlen, &addlen, &contid);
			rdtsc(end);
			assert(ret == 0);
			assert(usedlen == numobj*sizeof(*ids));
			assert(addlen == usedlen);
			assert(contid == 0);
			for (j = 0; j < numobj; j++) {
				ret = obj_delete_pid(osd->dbc, j+1);
				assert(ret == 0);
			}
		case 4: {
			numpid = numobj/32;
			if (numobj % 32 != 0)
				numpid++;
			for (j = 0; j < numpid; j++) {
				ret = obj_insert(osd->dbc, j+1, 0, PARTITION);
				assert(ret == 0);
				for (k = 1; k < 32+1; k++) {
					ret = obj_insert(osd->dbc, j+1, k, 
							 USEROBJECT);
					assert(ret == 0);
				}
			}
			cp = (uint8_t *)ids;
			usedlen = addlen = contid = 0;
			rdtsc(start);
			ret = obj_get_all_pids(osd->dbc, 0, 
					       sizeof(*ids)*numobj, cp,
					       &usedlen, &addlen, &contid);
			rdtsc(end);
			assert(ret == 0);
			assert(usedlen == numpid*sizeof(*ids));
			assert(addlen == usedlen);
			assert(contid == 0);
			for (j = 0; j < numpid; j++) {
				ret = obj_delete_pid(osd->dbc, j+1);
				assert(ret == 0);
			}
			numpid = 0;
			break;
		}
		default:
			fprintf(stderr, "1 <= test <= 4\n");
			exit(1);
		}

		t[i] = (double) (end - start) / mhz;
		start = end = 0;
	}

	mu = mean(t, numiter);
	sd = stddev(t, mu, numiter);
	printf("%s numiter %d numobj %d test, %d avg %lf +- %lf us\n", func,
	       numiter, numobj, test, mu, sd); 

	free(t);
	free(ids);
}

static void test_le(uint32_t page, uint32_t num, uint16_t len, 
		    const void *val, uint8_t *cp)
{
	uint8_t pad = 0;

	assert(get_ntohl(cp) == page), cp += 4;
	assert(get_ntohl(cp) == num), cp += 4;
	assert(get_ntohs(cp) == len), cp += 2;
	assert(memcmp(cp, val, len) == 0), cp += len;
	pad = roundup8(10+len) - (10+len);
	while (pad--)
		assert(*cp == 0), cp++;
}

static void pre_create_attrs(struct osd_device *osd, int numpg, int numattr)
{
	int ret;
	int np = 0;
	int na = 0;
	uint64_t val = 0;

	for (np = 1; np < numpg+1; np++) {
		for (na = 1; na < numattr+1; na++) {
			val = na;
			ret = attr_set_attr(osd->dbc, 1, 1, np, na, &val, 
					    sizeof(val));
			assert(ret == 0);
		}
	}
}

static void time_attr(struct osd_device *osd, int numpg, int numattr, 
		      int numiter, int test, const char *func)
{
	int ret = 0;
	int i = 0;
	uint64_t start, end;
	uint32_t usedlen = 0;
	uint8_t *cp = 0;
	uint64_t val = 0;
	double *t = 0;
	double mu, sd;
	struct list_entry *attr = NULL, *vattr = NULL;
	const uint32_t le_sz = roundup8(18), vle_sz = roundup8(50);
	const char uidp[] = "        unidentified attributes page   ";

	if (test < 1 || test > 9)
		return;

	t = Calloc(numiter, sizeof(*t));
	attr = Calloc(numpg*numattr, le_sz);
	vattr = Calloc(numpg*numattr, vle_sz);
	if (!t || !attr)
		return;

	switch (test) {
	case 1:
	case 2:
	case 3:
		val = 4;
		ret = attr_set_attr(osd->dbc, 1, 1, 2, 22, &val, sizeof(val));
		assert(ret == 0);
		usedlen = 0;
		memset(attr, 0, le_sz);
		ret = attr_get_attr(osd->dbc, 1, 1, 2, 22, le_sz, attr, 
				    RTRVD_SET_ATTR_LIST, &usedlen);
		assert(ret == 0);
		assert(usedlen == le_sz);
		cp = (uint8_t *)attr;
		test_le(2, 22, sizeof(val), &val, cp);
		ret = attr_delete_attr(osd->dbc, 1, 1, 2, 22);
		assert(ret == 0);
		usedlen = 0;
		memset(attr, 0, le_sz);
		ret = attr_get_attr(osd->dbc, 1, 1, 2, 22, le_sz, attr, 
				    RTRVD_SET_ATTR_LIST, &usedlen);
		assert(ret == -ENOENT);
		break;
	case 4:
	case 5:
	case 6:
		if (numpg*numattr < 2) {
			fprintf(stderr, "numpg*numattr < 2\n");
			return;
		}
		val = 200;
		ret = attr_set_attr(osd->dbc, 1, 1, 2, 22, &val, sizeof(val));
		assert(ret == 0);
		val = 400;
		ret = attr_set_attr(osd->dbc, 1, 1, 4, 44, &val, sizeof(val));
		assert(ret == 0);
		usedlen = 0;
		memset(attr, 0, 2*le_sz);
		ret = attr_get_all_attrs(osd->dbc, 1, 1, 2*le_sz, attr, 
				    RTRVD_SET_ATTR_LIST, &usedlen);
		assert(ret == 0);
		assert(usedlen == 2*le_sz);
		val = 200;
		cp = (uint8_t *)attr;
		test_le(2, 22, sizeof(val), &val, cp);
		val = 400;
		cp += le_sz;
		test_le(4, 44, sizeof(val), &val, cp);
		ret = attr_delete_all(osd->dbc, 1, 1);
		assert(ret == 0);
		usedlen = 0;
		memset(attr, 0, 2*le_sz);
		ret = attr_get_attr(osd->dbc, 1, 1, 2, 22, le_sz, attr, 
				    RTRVD_SET_ATTR_LIST, &usedlen);
		assert(ret == -ENOENT);
		ret = attr_get_attr(osd->dbc, 1, 1, 4, 44, le_sz, attr, 
				    RTRVD_SET_ATTR_LIST, &usedlen);
		assert(ret == -ENOENT);
		break;
	case 7:
		if (numpg*numattr < 4) {
			fprintf(stderr, "numpg*numattr < 4\n");
			goto out;
		}
		val = 200;
		ret = attr_set_attr(osd->dbc, 1, 1, 2, 22, &val, sizeof(val));
		assert(ret == 0);
		val = 400;
		ret = attr_set_attr(osd->dbc, 1, 1, 4, 44, &val, sizeof(val));
		assert(ret == 0);
		usedlen = 0;
		memset(vattr, 0, 2*vle_sz);
		ret = attr_get_dir_page(osd->dbc, 1, 1, USEROBJECT_PG,
					vle_sz*2, vattr, RTRVD_SET_ATTR_LIST,
					&usedlen);
		assert(ret == 0);
		assert(usedlen == vle_sz*2);
		cp = (uint8_t *)vattr;
		test_le(USEROBJECT_PG, 2, sizeof(uidp), uidp, cp);
		cp += vle_sz;
		test_le(USEROBJECT_PG, 4, sizeof(uidp), uidp, cp);
		ret = attr_delete_all(osd->dbc, 1, 1);
		assert(ret == 0);
		usedlen = 0;
		memset(attr, 0, 2*le_sz);
		ret = attr_get_attr(osd->dbc, 1, 1, 2, 22, le_sz, attr, 
				    RTRVD_SET_ATTR_LIST, &usedlen);
		assert(ret == -ENOENT);
		ret = attr_get_attr(osd->dbc, 1, 1, 4, 44, le_sz, attr, 
				    RTRVD_SET_ATTR_LIST, &usedlen);
		assert(ret == -ENOENT);
		break;
	case 8:
		if (numpg*numattr < 4) {
			fprintf(stderr, "numpg*numattr < 4\n");
			goto out;
		}
		for (i = 0; i < 4; i++) {
			val = i*100 + 1;
			ret = attr_set_attr(osd->dbc, 1, 1, i, 1, &val, 
					    sizeof(val));
			assert(ret == 0);
			ret = attr_set_attr(osd->dbc, 1, 1, i, 2, &val, 
					    sizeof(val));
			assert(ret == 0);
		}
		usedlen = 0;
		memset(attr, 0, 4*le_sz);
		ret = attr_get_for_all_pages(osd->dbc, 1, 1, 1, 4*le_sz, attr,
					     RTRVD_SET_ATTR_LIST, &usedlen);
		assert(ret == 0);
		assert(usedlen == 4*le_sz);
		cp = (uint8_t *)attr;
		for (i = 0; i < 4; i++) {
			val = i*100 + 1;
			test_le(i, 1, sizeof(val), &val, cp);
			cp += le_sz;
		}
		ret = attr_delete_all(osd->dbc, 1, 1);
		assert(ret == 0);
		usedlen = 0;
		memset(attr, 0, 4*le_sz);
		for (i = 0; i < 4; i++) {
			ret = attr_get_attr(osd->dbc, 1, 1, i, 1, le_sz, attr, 
					    RTRVD_SET_ATTR_LIST, &usedlen);
			assert(ret == -ENOENT);
			ret = attr_get_attr(osd->dbc, 1, 1, i, 2, le_sz, attr, 
					    RTRVD_SET_ATTR_LIST, &usedlen);
			assert(ret == -ENOENT);
		}
		break;
	case 9:
		if (numpg*numattr < 2) {
			fprintf(stderr, "numpg*numattr < 2\n");
			goto out;
		}
		for (i = 0; i < 2; i++) {
			val = i*100 + 1;
			ret = attr_set_attr(osd->dbc, 1, 1, i, 1, &val, 
					    sizeof(val));
			assert(ret == 0);
			ret = attr_set_attr(osd->dbc, 1, 1, i, 2, &val, 
					    sizeof(val));
			assert(ret == 0);
		}
		usedlen = 0;
		memset(attr, 0, 2*le_sz);
		ret = attr_get_page_as_list(osd->dbc, 1, 1, 1, 2*le_sz, attr,
					    RTRVD_SET_ATTR_LIST, &usedlen);
		assert(ret == 0);
		assert(usedlen == 2*le_sz);
		cp = (uint8_t *)attr;
		val = 101;
		test_le(1, 1, 8, &val, cp);
		cp += le_sz;
		test_le(1, 2, 8, &val, cp);
		ret = attr_delete_all(osd->dbc, 1, 1);
		assert(ret == 0);
		usedlen = 0;
		memset(attr, 0, 2*le_sz);
		for (i = 0; i < 2; i++) {
			ret = attr_get_attr(osd->dbc, 1, 1, i, 1, le_sz, attr, 
					    RTRVD_SET_ATTR_LIST, &usedlen);
			assert(ret == -ENOENT);
			ret = attr_get_attr(osd->dbc, 1, 1, i, 2, le_sz, attr, 
					    RTRVD_SET_ATTR_LIST, &usedlen);
			assert(ret == -ENOENT);
		}
		break;
	default:
		goto out;
	}

	for (i = 0; i < numiter;  i++) {
		/* set up; time test no. 4 */
		t[i] = 0.0;

		switch (test) {
		case 2:
		case 3:
			/* set one attr used to get/del */
			val = 400;
			ret = attr_set_attr(osd->dbc, 2, 2, 1, 1, &val,
					    sizeof(val));
			assert(ret == 0);
		case 1:
		case 5:
		case 6:
		case 7:
		case 8:
		case 9:
			pre_create_attrs(osd, numpg, numattr); 
			break;
		case 4: {
			int np, na;
			for (np = 1; np < numpg+1; np++) {
				for (na = 1; na < numattr+1; na++) {
					val = na;
					rdtsc(start);
					ret = attr_set_attr(osd->dbc, 1, 1,
							    np, na, &val, 
							    sizeof(val));
					rdtsc(end);
					assert(ret == 0);
					t[i] += (double)(end - start) / mhz;
				}
			}
			break;
		}
		default:
			goto out;
		}

		/* test */
		switch (test) {
		case 1:
			val = 400;
			rdtsc(start);
			ret = attr_set_attr(osd->dbc, 2, 2, 1, 1, 
					    &val, sizeof(val));
			rdtsc(end);
			assert(ret == 0);
			break;
		case 2:
			usedlen = 0;
			memset(attr, 0, le_sz);
			rdtsc(start);
			ret = attr_get_attr(osd->dbc, 2, 2, 1, 1, le_sz, attr, 
					    RTRVD_SET_ATTR_LIST, &usedlen);
			rdtsc(end);
			assert(ret == 0);
			assert(usedlen == le_sz);
			break;
		case 3:
			usedlen = 0;
			memset(attr, 0, le_sz);
			rdtsc(start);
			ret = attr_delete_attr(osd->dbc, 2, 2, 1, 1);
			rdtsc(end);
			assert(ret == 0);
			break;
		case 4:
			break;
		case 5:
			rdtsc(start);
			ret = attr_get_all_attrs(osd->dbc, 1, 1,
						 numpg*numattr*le_sz, attr,
						 RTRVD_SET_ATTR_LIST,
						 &usedlen);
			rdtsc(end);
			assert(ret == 0);
			assert(usedlen == (numpg*numattr*le_sz));
			break;
		case 6:
			rdtsc(start);
			ret = attr_delete_all(osd->dbc, 1, 1);
			rdtsc(end);
			assert(ret == 0);
			break;
		case 7:
			rdtsc(start);
			ret = attr_get_dir_page(osd->dbc, 1, 1, USEROBJECT_PG,
						numpg*vle_sz, vattr, 
						RTRVD_SET_ATTR_LIST, 
						&usedlen);
			rdtsc(end);
			assert(ret == 0);
			assert(usedlen == numpg*vle_sz);
			break;
		case 8:
			rdtsc(start);
			ret = attr_get_for_all_pages(osd->dbc, 1, 1, 1,
						     numpg*le_sz, attr,
						     RTRVD_SET_ATTR_LIST,
						     &usedlen);
			rdtsc(end);
			assert(ret == 0);
			assert(usedlen == numpg*le_sz);
			break;
		case 9:
			rdtsc(start);
			ret = attr_get_page_as_list(osd->dbc, 1, 1, 1,
						    numattr*le_sz, attr,
						    RTRVD_SET_ATTR_LIST,
						    &usedlen);
			rdtsc(end);
			assert(ret == 0);
			assert(usedlen == numattr*le_sz);
			break;
		default:
			goto out;
		}

		if (test != 4)
			t[i] = (double) (end - start) / mhz;
		end = start = 0;

		/* cleanup */
		switch (test) {
		case 1:
		case 2:
			ret = attr_delete_attr(osd->dbc, 2, 2, 1, 1);
			assert(ret == 0);
		case 3:
		case 4:
		case 5:
			break;
		case 6:
		case 7:
		case 8:
		case 9:
			ret = attr_delete_all(osd->dbc, 1, 1);
			assert(ret == 0);
			break;
		default:
			goto out;
		}
	}

	mu = mean(t, numiter);
	sd = stddev(t, mu, numiter);
	printf("%s numiter %d numpg %d numattr %d test %d avg %lf +- %lf "
	       " us\n", func, numiter, numpg, numattr, test, mu, sd); 

out:
	free(t);
	free(attr);
	free(vattr);
}

static void usage(void)
{
	fprintf(stderr, "\nUsage: ./%s [-o <numobj>] [-p <numpg>]"
		" [-a numattr] [-i <numiter>]"
		" \n\t\t [-t <timing-test>]\n\n", osd_get_progname());
	fprintf(stderr, "Option -t takes following values:\n");
	fprintf(stderr, "%16s: cumulative time for numobj insert in coll\n", 
		"collinsert");
	fprintf(stderr, "%16s: cumulative time for numobj delete in coll\n", 
		"colldelete");
	fprintf(stderr, "%16s: time to insert one after numobj in coll\n", 
		"collinsertone");
	fprintf(stderr, "%16s: time to delete one after numobj in coll\n", 
		"colldeleteone");
	fprintf(stderr, "%16s: cumulative time for numobj insert in obj\n", 
		"objinsert");
	fprintf(stderr, "%16s: cumulative time for numobj delete in obj\n", 
		"objdelete");
	fprintf(stderr, "%16s: time to insert one after numobj in obj\n", 
		"objinsertone");
	fprintf(stderr, "%16s: time to delete one after numobj in obj\n", 
		"objdeleteone");
	fprintf(stderr, "%16s: time to delete pid containing numobj\n", 
		"objdelpid");
	fprintf(stderr, "%16s: time to get nextpid after numobj pids\n", 
		"objnextpid");
	fprintf(stderr, "%16s: time to get nextoid after numobj\n", 
		"objnextoid");
	fprintf(stderr, "%16s: time to check existing object in numobj\n", 
		"objpresent");
	fprintf(stderr, "%16s: time to check nonexisting object in numobj\n", 
		"objnotpresent");
	fprintf(stderr, "%16s: time to check if pid with numobj is empty\n", 
		"objpidempty");
	fprintf(stderr, "%16s: time to get objtype in presence of numobj\n",
		"objgettype");
	fprintf(stderr, "%16s: time to get numobj oids\n", "objgetoids");
	fprintf(stderr, "%16s: time to get numobj cids\n", "objgetcids");
	fprintf(stderr, "%16s: time to get numobj pids\n", "objgetpids");
	fprintf(stderr, "%16s: time to get all pids; each pid is full\n",
		"objgetfullpids");
	fprintf(stderr, "%16s: time to set one attr after numobj*numattr\n",
		"attrsetone");
	fprintf(stderr, "%16s: time to get one attr after numobj*numattr\n",
		"attrgetone");
	fprintf(stderr, "%16s: time to del one attr after numobj*numattr\n",
		"attrdelone");
	fprintf(stderr, "%16s: cumulative time to set all numobj*numattr\n",
		"attrset");
	fprintf(stderr, "%16s: cumulative time to get all numobj*numattr\n",
		"attrget");
	fprintf(stderr, "%16s: cumulative time to del all numobj*numattr\n",
		"attrdel");
	fprintf(stderr, "%16s: time to get dir pages after numobj*numattr\n",
		"attrdirpg");
	fprintf(stderr, "%16s: time to get forall pg after numobj*numattr\n",
		"attrforallpg");
	fprintf(stderr, "%16s: time to get pg as list after numobj*numattr\n",
		"attrpgaslst");
	exit(1);
}

int main(int argc, char **argv)
{
	int ret = 0;
	int numobj = 10;
	int numiter = 10;
	int numattr = 10;
	int numpg = 10;
	const char *root = "/tmp/osd/";
	const char *func = NULL;
	struct osd_device osd;

	osd_set_progname(argc, argv);
	while (++argv, --argc > 0) {
		const char *s = *argv;
		if (s[0] == '-') {
			switch (s[1]) {
			case 'i':
				++argv, --argc;
				if (argc < 1)
					usage();
				numiter = atoi(*argv);
				break;
			case 'o':
				++argv, --argc;
				if (argc < 1)
					usage();
				numobj = atoi(*argv);
				break;
			case 'a':
				++argv, --argc;
				if (argc < 1)
					usage();
				numattr = atoi(*argv);
				break;
			case 'p':
				++argv, --argc;
				if (argc < 1)
					usage();
				numpg = atoi(*argv);
				break;
			case 't':
				++argv, --argc;
				if (argc < 1)
					usage();
				func = *argv;
				break;
			default:
				usage();
			}
		}
	}

	if (!func)
		usage();

	system("rm -rf /tmp/osd/*");
	ret = osd_open(root, &osd);
	assert(ret == 0);

  	if (!strcmp(func, "collinsert")) {
		time_coll_insert(&osd, numobj, numiter, 0);
	} else if (!strcmp(func, "collinsertone")) {
		time_coll_insert(&osd, numobj, numiter, 1);
	} else if (!strcmp(func, "colldelete")) {
		time_coll_delete(&osd, numobj, numiter, 0);
	} else if (!strcmp(func, "colldeleteone")) {
		time_coll_delete(&osd, numobj, numiter, 1);
	} else if (!strcmp(func, "objinsert")) {
		time_obj_insert(&osd, numobj, numiter, 0);
	} else if (!strcmp(func, "objinsertone")) {
		time_obj_insert(&osd, numobj, numiter, 1);
	} else if (!strcmp(func, "objdelete")) {
		time_obj_delete(&osd, numobj, numiter, 0);
	} else if (!strcmp(func, "objdeleteone")) {
		time_obj_delete(&osd, numobj, numiter, 1);
	} else if (!strcmp(func, "objdelpid")) {
		time_obj_delpid(&osd, numobj, numiter);
	} else if (!strcmp(func, "objnextpid")) {
		time_obj_generic(&osd, numobj, numiter, 1);
	} else if (!strcmp(func, "objnextoid")) {
		time_obj_generic(&osd, numobj, numiter, 2);
	} else if (!strcmp(func, "objpresent")) {
		time_obj_generic(&osd, numobj, numiter, 3);
	} else if (!strcmp(func, "objnotpresent")) {
		time_obj_generic(&osd, numobj, numiter, 4);
	} else if (!strcmp(func, "objpidempty")) {
		time_obj_generic(&osd, numobj, numiter, 5);
	} else if (!strcmp(func, "objgettype")) {
		time_obj_generic(&osd, numobj, numiter, 6);
	} else if (!strcmp(func, "objgetoids")) {
		time_obj_fetch(&osd, numobj, numiter, 1);
	} else if (!strcmp(func, "objgetcids")) {
		time_obj_fetch(&osd, numobj, numiter, 2);
	} else if (!strcmp(func, "objgetpids")) {
		time_obj_fetch(&osd, numobj, numiter, 3);
	} else if (!strcmp(func, "objgetfullpids")) {
		time_obj_fetch(&osd, numobj, numiter, 4);
	} else if (!strcmp(func, "attrsetone")) {
		time_attr(&osd, numpg, numattr, numiter, 1, func);
	} else if (!strcmp(func, "attrgetone")) {
		time_attr(&osd, numpg, numattr, numiter, 2, func);
	} else if (!strcmp(func, "attrdelone")) {
		time_attr(&osd, numpg, numattr, numiter, 3, func);
	} else if (!strcmp(func, "attrset")) {
		time_attr(&osd, numpg, numattr, numiter, 4, func);
	} else if (!strcmp(func, "attrget")) {
		time_attr(&osd, numpg, numattr, numiter, 5, func);
	} else if (!strcmp(func, "attrdel")) {
		time_attr(&osd, numpg, numattr, numiter, 6, func);
	} else if (!strcmp(func, "attrdirpg")) {
		time_attr(&osd, numpg, numattr, numiter, 7, func);
	} else if (!strcmp(func, "attrforallpg")) {
		time_attr(&osd, numpg, numattr, numiter, 8, func);
	} else if (!strcmp(func, "attrpgaslst")) {
		time_attr(&osd, numpg, numattr, numiter, 9, func);
	} else {
		usage();
	} 

	ret = osd_close(&osd);
	assert(ret == 0);

	return 0;
}
