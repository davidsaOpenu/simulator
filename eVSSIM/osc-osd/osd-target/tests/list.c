/*
 * Test LIST speed.  Either with or without retrieved attributes.
 */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <limits.h>

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

static void list_speed(struct osd_device *osd, int numobj, int numiter)
{
	struct osd_command c;
	int i;
	uint64_t start, end;
	double *v;
	uint64_t pid = PARTITION_PID_LB;
	uint64_t *pidlist;
	double mu, sd;
	uint64_t fac = 0, rem = 0;

	v = Malloc(numiter * sizeof(*v));
	if (!v)
		return;
	pidlist = Malloc(numobj * sizeof(*pidlist));
	if (!pidlist)
		return;

	osd_command_set_create_partition(&c, pid);
	run(osd, &c);

	fac = numobj / USHRT_MAX;
	rem = numobj % USHRT_MAX;
	while (fac--) {
		osd_command_set_create(&c, pid, 0, USHRT_MAX);
		run(osd, &c);
	}
	osd_command_set_create(&c, pid, 0, rem);
	run(osd, &c);

	osd_command_set_list(&c, pid, 0, 24 + (numobj * sizeof(*pidlist)), 
			     0, 0);

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
	printf("list numiter %d numobj %d avg %lf +/- %lf us\n", numiter,
	       numobj, mu, sd);
	free(pidlist);
	free(v);
}

#define ATTR_LEN 8

static void list_attr_speed(struct osd_device *osd, int numobj, int numiter,
			    int numattr, int numattr_retrieve)
{
	struct osd_command c;
	int i;
	uint64_t start, end;
	double *v;
	uint64_t pid = PARTITION_PID_LB;
	uint8_t *results;
	double mu, sd;
	struct attribute_list *attr;
	uint8_t *attr_val;
	uint64_t alloc_len;

	if (numattr_retrieve > numattr)
		return;

	v = Malloc(numiter * sizeof(*v));
	if (!v)
		return;
	/* XXX: 16 here for spec modifications for alignment; spec says 12 */
	alloc_len = 24 + numobj * (16 + numattr_retrieve
					* roundup8(10 + ATTR_LEN));
	results = Malloc(alloc_len);
	if (!results)
		return;

	attr = Malloc(numattr * sizeof(*attr));
	if (!attr)
		return;
	attr_val = Malloc(ATTR_LEN);
	if (!attr_val)
		return;
	memset(attr_val, 0x5a, ATTR_LEN);

	osd_command_set_create_partition(&c, pid);
	run(osd, &c);

	for (i=0; i<numattr; i++) {
		attr[i].type = ATTR_SET;
		attr[i].page = LUN_PG_LB;
		attr[i].number = 1 + i;  /* skip directory */
		attr[i].val = attr_val;
		attr[i].len = ATTR_LEN;
	}

	for (i=0; i<numobj; i++) {
		osd_command_set_create(&c, pid, 0, 1);
		osd_command_attr_build(&c, attr, numattr);
		run(osd, &c);
		osd_command_attr_free(&c);
	}

	for (i=0; i<numattr_retrieve; i++)
		attr[i].type = ATTR_GET;

	osd_command_set_list(&c, pid, 0, alloc_len, 0, 1);
	osd_command_attr_build(&c, attr, numattr_retrieve);

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
	printf("list_attr numiter %d numobj %d numattr %d numattr_retrieve %d"
	       " avg %lf +/- %lf us\n", numiter, numobj, numattr,
	       numattr_retrieve, mu, sd);
	free(attr_val);
	free(attr);
	free(results);
	free(v);
}

static void usage(void)
{
	fprintf(stderr, "Usage: %s [-o <numobj>] [-i <numiter>]\n",
		osd_get_progname());
	fprintf(stderr,
		"          [-a <numattr>] [-r <numattr to retrieve>]\n");
	fprintf(stderr, "Does LIST with list_attr == 0 for no -a/-r opts.\n");
	fprintf(stderr, "Does LIST with list_attr == 1 when -a and -r set.\n");
	exit(1);
}

int main(int argc, char **argv)
{
	int numiter = 100;
	int numobj = 100;
	int numattr = 0;
	int numattr_retrieve = 0;
	static struct osd_device osd;
	int ret;

	osd_set_progname(argc, argv);
	while (++argv, --argc > 0) {
		const char *s = *argv;
		if (s[0] == '-') {
			switch (s[1]) {
			case 'o':
				++argv, --argc;
				if (argc < 1)
					usage();
				numobj = atoi(*argv);
				break;
			case 'i':
				++argv, --argc;
				if (argc < 1)
					usage();
				numiter = atoi(*argv);
				break;
			case 'a':
				++argv, --argc;
				if (argc < 1)
					usage();
				numattr = atoi(*argv);
				break;
			case 'r':
				++argv, --argc;
				if (argc < 1)
					usage();
				numattr_retrieve = atoi(*argv);
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

	if (numattr && numattr_retrieve)
		list_attr_speed(&osd, numobj, numiter, numattr,
				numattr_retrieve);
	else
		list_speed(&osd, numobj, numiter);

	ret = osd_close(&osd);
	assert(ret == 0);

	return 0;
}

