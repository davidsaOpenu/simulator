/*
 * Test QUERY speed.
 */
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "osd-types.h"
#include "cdb.h"
#include "osd.h"
#include "osd-initiator/command.h"
#include "osd-util/osd-util.h"

static void run(struct osd_device *osd, struct osd_command *c)
{
	int ret;
	uint8_t *data_in = NULL;
	uint64_t data_in_len = 0;
	uint8_t sense_out[252];
	int senselen_out;

	ret = osdemu_cmd_submit(osd, c->cdb, c->outdata, c->outlen, &data_in,
				&data_in_len, sense_out, &senselen_out);
	assert(ret == 0);
}

#define ATTR_LEN 8

static void query_speed(struct osd_device *osd, int numiter, int numobj,
			int numobj_in_coll, int nummatch, int numattr,
			int numcriteria)
{
	struct osd_command c;
	int i;
	uint64_t start, end;
	double *v;
	uint64_t pid = PARTITION_PID_LB;
	uint64_t cid = COLLECTION_OID_LB;
	uint8_t *results, *query;
	double mu, sd;
	struct attribute_list *attr;
	uint8_t *attr_val_lo, *attr_val_match, *attr_val_nomatch, *attr_val_hi;
	uint64_t alloc_len, query_len, criterion_len;
	uint8_t cidn[8], *cp;

	if (numobj_in_coll > numobj)
		return;
	if (nummatch > numobj_in_coll)
		return;
	if (numcriteria > numattr)
		return;

	v = Malloc(numiter * sizeof(*v));
	if (!v)
		return;

	alloc_len = 12 + numobj * 8;
	results = Malloc(alloc_len);
	if (!results)
		return;

	attr = Malloc((1 + numattr) * sizeof(*attr));
	if (!attr)
		return;
	attr_val_lo = Malloc(4 * ATTR_LEN);
	if (!attr_val_lo)
		return;
	attr_val_match   = attr_val_lo +     ATTR_LEN;
	attr_val_hi      = attr_val_lo + 2 * ATTR_LEN;
	attr_val_nomatch = attr_val_lo + 3 * ATTR_LEN;
	memset(attr_val_lo,      0x4b, ATTR_LEN);
	memset(attr_val_match,   0x5a, ATTR_LEN);
	memset(attr_val_hi,      0x69, ATTR_LEN);
	memset(attr_val_nomatch, 0x78, ATTR_LEN);

	osd_command_set_create_partition(&c, pid);
	run(osd, &c);

	osd_command_set_create_collection(&c, pid, cid);
	run(osd, &c);

	for (i=0; i<numattr; i++) {
		attr[i].type = ATTR_SET;
		attr[i].page = LUN_PG_LB;
		attr[i].number = 1 + i;  /* skip directory */
		attr[i].val = attr_val_nomatch;
		attr[i].len = ATTR_LEN;
	}

	/* one extra attr to stick it in the collection, for some of them */
	set_htonll(cidn, cid);
	attr[numattr].type = ATTR_SET;
	attr[numattr].page = USER_COLL_PG;
	attr[numattr].number = 1;
	attr[numattr].len = 8;
	attr[numattr].val = cidn;

	for (i=0; i<numobj; i++) {
		int j, this_numattr;

		osd_command_set_create(&c, pid, 0, 1);
		this_numattr = numattr;
		if (i < numobj_in_coll)
			++this_numattr;  /* stick in coll */
		/* some of the ones in the collection will match */
		for (j=0; j<numattr; j++)
			attr[j].val = (i < nummatch ? attr_val_match
						    : attr_val_nomatch);
		osd_command_attr_build(&c, attr, this_numattr);
		run(osd, &c);
		osd_command_attr_free(&c);
	}

	criterion_len = 12 + 2 + ATTR_LEN + 2 + ATTR_LEN;
	if (numcriteria == 0)
		query_len = 8;  /* special case, just one empty criterion */
	else
		query_len = 4 + numcriteria * criterion_len;
	query = Malloc(query_len);
	if (!query)
		return;
	memset(query, 0, query_len);
	query[0] = 1;  /* intersection */
	cp = query + 4;
	for (i=0; i<numcriteria; i++) {
		set_htons(cp + 2, criterion_len);
		set_htonl(cp + 4, LUN_PG_LB);
		set_htonl(cp + 8, 1 + i);
		set_htons(cp + 12, ATTR_LEN);
		memcpy(cp + 12 + 2, attr_val_lo, ATTR_LEN);
		set_htons(cp + 12 + 2 + ATTR_LEN, ATTR_LEN);
		memcpy(cp + 12 + 2 + ATTR_LEN + 2, attr_val_hi, ATTR_LEN);
		cp += criterion_len;
	}

	osd_command_set_query(&c, pid, cid, query_len, alloc_len);
	c.outdata = query;
	c.outlen = query_len;

	for (i=0; i<5; i++)
		run(osd, &c);

	for (i=0; i<numiter; i++) {
		rdtsc(start);
		run(osd, &c);
		rdtsc(end);
		v[i] = (double)(end - start) / mhz;
	}

	mu = mean(v, numiter);
	sd = stddev(v, mu, numiter);
	printf("query numiter %d numobj %d numobj_in_coll %d nummatch %d"
	       " numattr %d numcriteria %d avg %lf +/- %lf us\n",
	       numiter, numobj, numobj_in_coll, nummatch, numattr,
	       numcriteria, mu, sd);
	free(query);
	free(attr_val_lo);
	free(attr);
	free(results);
	free(v);
}

static void usage(void)
{
	fprintf(stderr, "Usage: %s [options]\n", osd_get_progname());
	fprintf(stderr, "  -i <numiter> : number of iterations\n");
	fprintf(stderr, "  -o <numobj> : total number of objects in device\n");
	fprintf(stderr, "  -c <numobj_in_coll> : number in the collection\n");
	fprintf(stderr, "  -m <nummatch> : number of those that will match\n");
	fprintf(stderr, "  -a <numattr> : total number of attr on each obj\n");
	fprintf(stderr, "  -n <numcriteria> : number of criteria\n");
	exit(1);
}

int main(int argc, char **argv)
{
	int numiter = 100;
	int numobj = 200;
	int numobj_in_coll = 100;
	int nummatch = 20;
	int numattr = 10;
	int numcriteria = 3;
	static struct osd_device osd;
	int ret;

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
			case 'c':
				++argv, --argc;
				if (argc < 1)
					usage();
				numobj_in_coll = atoi(*argv);
				break;
			case 'm':
				++argv, --argc;
				if (argc < 1)
					usage();
				nummatch = atoi(*argv);
				break;
			case 'a':
				++argv, --argc;
				if (argc < 1)
					usage();
				numattr = atoi(*argv);
				break;
			case 'n':
				++argv, --argc;
				if (argc < 1)
					usage();
				numcriteria = atoi(*argv);
				break;
			default:
				usage();
			}
		} else
			usage();
	}

	system("rm -rf /tmp/osd");
	ret = osd_open("/tmp/osd", &osd);
	assert(ret == 0);

	query_speed(&osd, numiter, numobj, numobj_in_coll, nummatch,
		    numattr, numcriteria);

	ret = osd_close(&osd);
	assert(ret == 0);

	return 0;
}

