/*
 * Test SET_MEMBER_ATTRIBUTES speed.
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

static void set_member_attributes_speed(struct osd_device *osd, int numiter,
					int numobj, int numobj_in_coll,
					int numattr, int numattr_set)
{
	struct osd_command c;
	int i;
	uint64_t start, end;
	double *v;
	uint64_t pid = PARTITION_PID_LB;
	uint64_t cid = COLLECTION_OID_LB;
	double mu, sd;
	struct attribute_list *attr;
	uint8_t *attr_val;
	uint8_t cidn[8];

	if (numobj_in_coll > numobj)
		return;
	if (numattr_set > numattr)
		return;

	v = Malloc(numiter * sizeof(*v));
	if (!v)
		return;

	attr = Malloc((numattr + 1) * sizeof(*attr));
	if (!attr)
		return;
	attr_val = Malloc(ATTR_LEN);
	if (!attr_val)
		return;
	memset(attr_val, 0x5a, ATTR_LEN);

	osd_command_set_create_partition(&c, pid);
	run(osd, &c);

	osd_command_set_create_collection(&c, pid, cid);
	run(osd, &c);

	for (i=0; i<numattr; i++) {
		attr[i].type = ATTR_SET;
		attr[i].page = LUN_PG_LB;
		attr[i].number = 1 + i;  /* skip directory */
		attr[i].val = attr_val;
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
		int this_numattr;

		osd_command_set_create(&c, pid, 0, 1);
		this_numattr = numattr;
		if (i < numobj_in_coll)
			++this_numattr;  /* stick in coll */
		osd_command_attr_build(&c, attr, this_numattr);
		run(osd, &c);
		osd_command_attr_free(&c);
	}

	osd_command_set_set_member_attributes(&c, pid, cid);
	osd_command_attr_build(&c, attr, numattr_set);

	for (i=0; i<5; i++)
		run(osd, &c);

	for (i=0; i<numiter; i++) {
		rdtsc(start);
		run(osd, &c);
		rdtsc(end);
		v[i] = (double)(end - start) / mhz;
	}

	osd_command_attr_free(&c);

	mu = mean(v, numiter);
	sd = stddev(v, mu, numiter);
	printf("set_member_attributes numiter %d numobj %d numobj_in_coll %d"
	       " numattr %d numattr_set %d"
	       " avg %lf +/- %lf us\n", numiter, numobj, numobj_in_coll,
	       numattr, numattr_set, mu, sd);
	free(attr_val);
	free(attr);
	free(v);
}

static void usage(void)
{
	fprintf(stderr, "Usage: %s [options]\n", osd_get_progname());
	fprintf(stderr, "  -i <numiter> : number of iterations\n");
	fprintf(stderr, "  -o <numobj> : total number of objects in device\n");
	fprintf(stderr, "  -c <numobj_in_coll> : number in the collection\n");
	fprintf(stderr, "  -a <numattr> : total number of attr on each obj\n");
	fprintf(stderr, "  -s <numattr_set> : number of those to set\n");
	exit(1);
}

int main(int argc, char **argv)
{
	int numiter = 100;
	int numobj = 200;
	int numobj_in_coll = 100;
	int numattr = 10;
	int numattr_set = 2;
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
			case 'a':
				++argv, --argc;
				if (argc < 1)
					usage();
				numattr = atoi(*argv);
				break;
			case 's':
				++argv, --argc;
				if (argc < 1)
					usage();
				numattr_set = atoi(*argv);
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

	set_member_attributes_speed(&osd, numiter, numobj, numobj_in_coll,
				    numattr, numattr_set);

	ret = osd_close(&osd);
	assert(ret == 0);

	return 0;
}

