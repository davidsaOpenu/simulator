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
	uint8_t *data_in = NULL;
	uint64_t data_in_len = 0;
	uint8_t sense_out[252];
	int senselen_out;

	ret = osdemu_cmd_submit(osd, c->cdb, c->outdata, c->outlen, &data_in,
				&data_in_len, sense_out, &senselen_out);
	assert(ret == 0);
}

static void create_speed(struct osd_device *osd, int numiter, uint64_t numobj)
{
	int i;
	double *v;
	double mu, sd;
	struct osd_command cmd;
	uint64_t start, end;
	uint64_t pid = PARTITION_PID_LB;
	uint64_t oid = 0;
	uint64_t fac = 0, rem = 0;

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

	/* warm up */
	for (i=0; i<10; i++) {
		osd_command_set_create(&cmd, pid, oid, 1);
		run(osd, &cmd);
		oid = osd->ccap.oid;
		osd_command_set_remove(&cmd, pid, oid);
		run(osd, &cmd);
	}

	for (i = 0; i < numiter; i++) {
		osd_command_set_create(&cmd, pid, oid, 1);
		
		rdtsc(start);
		run(osd, &cmd);
		rdtsc(end);
		v[i] = ((double) (end - start)) / mhz;  /* time in usec */

		oid = osd->ccap.oid;
		osd_command_set_remove(&cmd, pid, oid);
		run(osd, &cmd);
	}

	mu = mean(v, numiter);
	sd = stddev(v, mu, numiter);
	/*
	 * XXX: Be sure to pay attention to the values, they creep up
	 * due to database overheads as the number of objects grows.
	 */
	printf("create numiter %d numobj %llu avg %9.3lf +- %8.3lf us\n", 
	       numiter, llu(numobj), mu, sd);
	if (0) {
		for (i=0; i<numiter; i++)
			printf("%9.3lf\n", v[i]);
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
	
	create_speed(&osd, numiter, numobj);

	ret = osd_close(&osd);
	assert(ret == 0);

	return 0;
}

