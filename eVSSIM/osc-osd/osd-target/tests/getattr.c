#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <limits.h>

#include "osd-types.h"
#include "cdb.h"
#include "osd.h"
#include "osd-initiator/command.h"
#include "osd-util/osd-util.h"

static inline void run(struct osd_device *osd, struct osd_command *c)
{
	int ret;
	uint8_t sense_out[252];
	uint8_t *data_in = NULL;
	uint64_t data_in_len = 0;
	int senselen_out;

	ret = osdemu_cmd_submit(osd, c->cdb, c->outdata, c->outlen, &data_in,
				&data_in_len, sense_out, &senselen_out);
	assert(ret == 0);
	c->inlen = data_in_len;
	if (c->indata)
		memcpy(c->indata, data_in, c->inlen);
	free(data_in);
}

static void getattr_speed(struct osd_device *osd, int numiter, 
			  uint64_t numobj)
{
	int i;
	double *v;
	double mu, sd;
	struct osd_command cmd;
	uint64_t start, end;
	uint64_t pid = PARTITION_PID_LB;
	uint64_t oid = USEROBJECT_OID_LB;
	uint64_t fac = 0, rem = 0;
	char attr_val[] = "deadbEEf";
	char ret_val[16];
	struct attribute_list attr = {
		.type = ATTR_SET,
		.page = USEROBJECT_PG + LUN_PG_LB,
		.number = 1,
		.val = attr_val,
		.len = sizeof(attr_val),
	};

	v = malloc(numiter * sizeof(*v));
	if (!v)
		osd_error_fatal("out of memory");

	osd_command_set_create_partition(&cmd, pid);
	run(osd, &cmd);

	fac = numobj / USHRT_MAX;
	rem = numobj % USHRT_MAX;
	while (fac--) {
		osd_command_set_create(&cmd, pid, 0, USHRT_MAX);
		run(osd, &cmd);
	}
	osd_command_set_create(&cmd, pid, 0, rem);
	run(osd, &cmd);
	oid = osd->ccap.oid + 1;

	v = malloc(numiter * sizeof(*v));
	if (!v)
		osd_error_fatal("out of memory");

	osd_command_set_create(&cmd, pid, 0, 1);
	run(osd, &cmd);
	oid = osd->ccap.oid;

	osd_command_set_set_attributes(&cmd, pid, oid);
	osd_command_attr_build(&cmd, &attr, 1);
	run(osd, &cmd);
	osd_command_attr_free(&cmd);

	attr.type = ATTR_GET;
	attr.val = ret_val;
	memset(ret_val, 0, sizeof(ret_val));
	osd_command_set_get_attributes(&cmd, pid, oid);
	osd_command_attr_build(&cmd, &attr, 1);
	run(osd, &cmd);
	osd_command_attr_resolve(&cmd);
	assert(memcmp(attr_val, cmd.attr[0].val, sizeof(attr_val)) == 0);
	osd_command_attr_free(&cmd);

	attr.type = ATTR_GET;
	attr.val = ret_val;
	memset(ret_val, 0, sizeof(ret_val));
	osd_command_set_get_attributes(&cmd, pid, oid);
	osd_command_attr_build(&cmd, &attr, 1);
	/* warm up */
	for (i = 0; i < 10; i++) {
		run(osd, &cmd);
	}

	for (i=0; i<numiter; i++) {
		rdtsc(start);
		run(osd, &cmd);
		rdtsc(end);

		v[i] = ((double) (end - start)) / mhz;  /* time in usec */
	}
	osd_command_attr_free(&cmd);

	mu = mean(v, numiter);
	sd = stddev(v, mu, numiter);

	printf("getattr numiter %d numobj %llu avg %9.3lf +- %8.3lf us\n", 
	       numiter, llu(numobj), mu, sd);
	if (0) {
		for (i=0; i<numiter; i++) 
			printf("latency %lf\n", v[i]);
	}
	free(v);
}

static void usage(void)
{
	fprintf(stderr, "Usage: %s [-o <numobj>] [-i <numiter>]\n", 
		osd_get_progname());
	exit(1);
}

int main(int argc, char **argv)
{
	int ret = 0;
	int numiter = 10;
	int numobj = 10;
	static struct osd_device osd;

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
			default:
				usage();
			}
		} else {
			usage();
		}
	}

	system("rm -rf /tmp/osd");
	ret = osd_open("/tmp/osd", &osd);
	assert(ret == 0);
	
	getattr_speed(&osd, numiter, numobj);

	ret = osd_close(&osd);
	assert(ret == 0);

	return 0;
}

