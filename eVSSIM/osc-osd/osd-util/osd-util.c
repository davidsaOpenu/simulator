/*
 * Mix of handy utilities.
 *
 * Copyright (C) 2000-7 Pete Wyckoff <pw@osc.edu>
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
#include <stdarg.h>
#include <errno.h>
#include <unistd.h>
#include <math.h>
#include <ctype.h>
#include <time.h>
#include <sys/time.h>

#include "osd-util.h"
#include "osd-defs.h"

/* global */
static const char *progname = "(pre-main)";
double mhz = -1.0;

/*
 * Set the program name, first statement of code usually.
 */
void osd_set_progname(int argc __attribute__((unused)), char *const argv[])
{
	const char *cp;

	for (cp=progname=argv[0]; *cp; cp++)
		if (*cp == '/')
			progname = cp+1;
	/* for timing tests */
	mhz = get_mhz();
}

const char *osd_get_progname(void)
{
	return progname;
}

/*
 * Debugging.
 */
void __attribute__((format(printf,1,2)))
osd_info(const char *fmt, ...)
{
	va_list ap;
	struct timeval tv;
	time_t tp;
	char buffer[16];

	gettimeofday(&tv, NULL);
	tp = tv.tv_sec;
	strftime(buffer, 9, "%H:%M:%S", localtime(&tp));
	sprintf(buffer+8, ".%06ld", (long) tv.tv_usec);

	fprintf(stderr, "%s: [%s] ", progname, buffer);
	va_start(ap, fmt);
	vfprintf(stdout, fmt, ap);
	va_end(ap);
	fprintf(stdout, ".\n");
}

/*
 * Warning, non-fatal.
 */
void __attribute__((format(printf,1,2)))
osd_warning(const char *fmt, ...)
{
	va_list ap;

	fprintf(stdout, "%s: Warning: ", progname);
	va_start(ap, fmt);
	vfprintf(stdout, fmt, ap);
	va_end(ap);
	fprintf(stdout, ".\n");
}

/*
 * Error.
 */
void __attribute__((format(printf,1,2)))
osd_error(const char *fmt, ...)
{
	va_list ap;

	fprintf(stderr, "%s: Error: ", progname);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, ".\n");
}

/*
 * Error with the errno message.
 */
void __attribute__((format(printf,1,2)))
osd_error_errno(const char *fmt, ...)
{
	va_list ap;

	fprintf(stderr, "%s: Error: ", progname);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, ": %s.\n", strerror(errno));
}

/*
 * Errno with the message corresponding to this explict -errno value.
 * It should be negative.
 */
void __attribute__((format(printf,2,3)))
osd_error_xerrno(int errnum, const char *fmt, ...)
{
	va_list ap;

	fprintf(stderr, "%s: Error: ", progname);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, ": %s.\n", strerror(-errnum));
}

/*
 * Error, fatal with the errno message.
 */
void __attribute__((format(printf,1,2)))
osd_error_fatal(const char *fmt, ...)
{
	va_list ap;

	fprintf(stderr, "%s: Error: ", progname);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, ": %s.\n", strerror(errno));
	exit(1);
}

/*
 * Error-checking malloc.
 */
void * __attribute__((malloc))
Malloc(size_t n)
{
	void *x = NULL;

	if (n == 0) {
		osd_error("%s: called on zero bytes", __func__);
	} else {
		x = malloc(n);
		if (!x)
			osd_error("%s: couldn't get %lu bytes", __func__, 
				  (unsigned long) n);
	}
	return x;
}

/*
 * Error-checking counted cleared memory.
 */
void * __attribute__((malloc))
Calloc(size_t nmemb, size_t n)
{
	void *x = NULL;

	if (n == 0)
		osd_error("%s: called on zero bytes", __func__);
	else {
		x = calloc(nmemb, n);
		if (!x)
			osd_error("%s: couldn't get %zu bytes", __func__, 
				  nmemb * n);
	}
	return x;
}

/*
 * For reading from a pipe, can't always get the full buf in one chunk.
 */
size_t osd_saferead(int fd, void *buf, size_t num)
{
	int i, offset = 0;
	int total = num;

	while (num > 0) {
		i = read(fd, (char *)buf + offset, num);
		if (i < 0)
			osd_error_errno("%s: read %zu bytes", __func__, num);
		if (i == 0) {
			if (offset == 0) 
				return 0; /* end of file on a block boundary */
			osd_error("%s: EOF, only %d of %d bytes", __func__, 
				  offset, total);
		}
		num -= i;
		offset += i;
	}
	return total;
}

size_t osd_safewrite(int fd, const void *buf, size_t num)
{
	int i, offset = 0;
	int total = num;

	while (num > 0) {
		i = write(fd, (const char *)buf + offset, num);
		if (i < 0)
			osd_error_errno("%s: write %zu bytes", __func__, num);
		if (i == 0)
			osd_error("%s: EOF, only %d of %d bytes", __func__,
				  offset, total);
		num -= i;
		offset += i;
	}
	return total;
}

/*
 * Debugging.
 */
void osd_hexdump(const void *dv, size_t len)
{
	const uint8_t *d = dv;
	size_t offset = 0;

	while (offset < len) {
		unsigned int i, range;

		range = 8;
		if (range > len-offset)
			range = len-offset;
		printf("%4zx:", offset);
		for (i=0; i<range; i++)
			printf(" %02x", d[offset+i]);
		printf("  ");
		for (i=range; i<8; i++)
		    printf("   ");
		for (i=0; i<range; i++)
			printf("%c", isprint(d[offset+i]) ? d[offset+i] : '.');
		printf("\n");
		offset += range;
	}
}

/* endian functions */
static uint32_t swab32(uint32_t d)
{
	return  (d & (uint32_t) 0x000000ffUL) << 24 |
		(d & (uint32_t) 0x0000ff00UL) << 8  |
		(d & (uint32_t) 0x00ff0000UL) >> 8  |
		(d & (uint32_t) 0xff000000UL) >> 24;
}

/*
 * Things are not aligned in the current osd2r00, but they probably
 * will be soon.  Assume 4-byte alignment though.
 */
uint64_t get_ntohll_le(const void *d)
{
	uint32_t d0 = swab32(*(const uint32_t *) d);
	uint32_t d1 = swab32(*(const uint32_t *) ((long)d + 4));

	return (uint64_t) d0 << 32 | d1;
}

/*
 * Doing these without memcpy for alignment now.  If anyone actually runs
 * on a BE machine, perhaps they'll tell us if alignment is needed.
 */
uint64_t get_ntohll_be(const void *d)
{
	return *(const uint64_t *) d;
}

uint32_t get_ntohl_le(const void *d)
{
	return swab32(*(const uint32_t *) d);
}

uint32_t get_ntohl_be(const void *d)
{
	return *(const uint32_t *) d;
}

uint16_t get_ntohs_le(const void *d)
{
	uint16_t x = *(const uint16_t *) d;

	return (x & (uint16_t) 0x00ffU) << 8 |
		(x & (uint16_t) 0xff00U) >> 8;
}

uint16_t get_ntohs_be(const void *d)
{
	return *(const uint16_t *) d;
}

void set_htonll_le(void *x, uint64_t val)
{
	uint32_t *xw = (uint32_t *) x;

	xw[0] = swab32((val & (uint64_t) 0xffffffff00000000ULL) >> 32);
	xw[1] = swab32((val & (uint64_t) 0x00000000ffffffffULL));
}

void set_htonll_be(void *x, uint64_t val)
{
	*(uint64_t *) x = val;
}

void set_htonl_le(void *x, uint32_t val)
{
	uint32_t *xw = (uint32_t *) x;

	*xw = swab32(val);
}

void set_htonl_be(void *x, uint32_t val)
{
	*(uint32_t *) x = val;
}

void set_htons_le(void *x, uint16_t val)
{
	uint16_t *xh = (uint16_t *) x;

	*xh = (val & (uint16_t) 0x00ffU) << 8 |
		(val & (uint16_t) 0xff00U) >> 8;
}

void set_htons_be(void *x, uint16_t val)
{
	*(uint16_t *) x = val;
}

/*
 * Offset fields for attribute lists are floating point-ish.
 *
 * Returns mantissa * (2^(exponent+8)) where
 *	unsigned mantissa : 28;
 *	int	 exponent : 04;
 * d points to 32bit __be32 osd_offset value
 */
uint64_t get_ntohoffset(const void *d)
{
	const uint32_t mask = 0xf0000000UL;
	uint32_t base;
	int32_t exponent;
	uint64_t x;

	base = get_ntohl(d);
	if (base == OFFSET_UNUSED)
		return ~0;

	exponent = base ;
	exponent >>= 28; /* sign extend right shift */

	x = (uint64_t) (base & ~mask) << (exponent + 8);
	return x;
}

/*
 * This assumes that val is already rounded.  Else it loses bits as
 * it converts, effectively truncating.  These generally try to use
 * the smallest possible exponent to accommodate the value.
 */
void set_htonoffset(void *x, uint64_t val)
{
	const uint64_t max_mantissa = 0x0fffffffULL;
	uint64_t start = val;
	uint32_t base;
	int32_t exponent;

	exponent = -5;
	val >>= 3;
	while (val > max_mantissa) {
		++exponent;
		val >>= 1;
	}
	if (exponent > 15) {
		osd_error("%s: offset 0x%llx too big for format", __func__,
		          llu(start));
		memset(x, 0, 4);
		return;
	}
	base = val;
	base |= exponent << 28;
	set_htonl(x, base);
}

/*
 * Find the next legal offset >= start.
 */
uint64_t next_offset(uint64_t start)
{
	const uint64_t max_mantissa = 0x0fffffffULL;
	uint64_t val;
	unsigned exponent;

	exponent = 3;
	val = start;
	val >>= exponent;
	while (val > max_mantissa) {
		++exponent;
		val >>= 1;
	}
recheck:
	if (exponent > 23) {
		osd_error("%s: offset 0x%llx too big for format", __func__,
		          llu(start));
		return 0;
	}

	/*
	 * Now we have found floor(val * 2^exponent) for start, but this
	 * could be less than start.
	 */
	if (val << exponent < start) {
		val += 1;
		if (val & ~max_mantissa) {
			/* roll to the next exponent */
			++exponent;
			val >>= 1;
			goto recheck;
		}
	}
	return val << exponent;
}

#ifdef TEST_NEXT_OFFSET
int main(void)
{
	struct {
		uint64_t start;
		uint64_t want;
	} v[] = {
		{ 0x0000000000000000ULL, 0x0000000000000000ULL },
		{ 0x0000000000000001ULL, 0x0000000000000100ULL },
		{ 0x0000000000000101ULL, 0x0000000000000200ULL },
		{ 0x0000000fffffff00ULL, 0x0000000fffffff00ULL },
		{ 0x0000000fffffff01ULL, 0x0000001000000000ULL },
		{ 0x0000000000800001ULL, 0x0000000000800100ULL },
		{ 0x0007ffffff800000ULL, 0x0007ffffff800000ULL },
		{ 0x0007ffffff800001ULL, 0x0000000000000000ULL }, /* too big */
		{ 0xffffffffffffffffULL, 0x0000000000000000ULL }, /* too big */
	};
	int i;

	for (i=0; i<ARRAY_SIZE(v); i++) {
		char x[4];
		uint64_t out, conv;
		out = next_offset(v[i].start);
		set_htonoffset(x, out);
		conv = get_ntohoffset(x);
		printf("%016llx -> %016llx %016llx %016llx %s\n", v[i].start,
		       v[i].want, out, conv,
		       (out == v[i].want && out == conv) ? "ok" : "BAD");
	}
	return 0;
}
#endif

/*
 * Return time in ms since 1970, given a six-byte big-endian as encoded
 * in OSD.
 */
uint64_t get_ntohtime_le(const void *d)
{
	uint8_t s[8];

	s[0] = 0;
	s[1] = 0;
	memcpy(&s[2], d, 6);
	return get_ntohll_le(s);
}

uint64_t get_ntohtime_be(const void *d)
{
	union {
		uint8_t s[8];
		uint64_t t;
	} u;

	u.s[0] = 0;
	u.s[1] = 0;
	memcpy(&u.s[2], d, 6);
	return u.t;
}

/*
 * Ignore biggest two bytes, encode other six as big endian.
 */
void set_htontime_le(void *x, uint64_t val)
{
	uint8_t s[8];

	set_htonll_le(s, val);
	memcpy(x, s+2, 6);
}

void set_htontime_be(void *x, uint64_t val)
{
	union {
		uint8_t s[8];
		uint64_t t;
	} u;

	u.t = val;
	memcpy(x, u.s+2, 6);
}

double mean(double *v, int N)
{
	int i = 0;
	double mu = 0.0;

	for (i = 0; i < N; i++)
		mu += v[i];

	return mu/N;
}

double stddev(double *v, double mu, int N) 
{
	int i = 0;
	double sd = 0.0;

	if (N <= 1)
		return 0.0;

	for (i = 0; i < N; i++)
		sd += (v[i] - mu)*(v[i] - mu);
	sd = sqrt(sd / (N-1));
	return sd;
}


static double partition(double *a, int left, int right, int pi)
{
	int i, ind;
	double pv, tmp;
	
	pv = a[pi];

	tmp = a[right];
	a[right] = a[pi];
	a[pi] = tmp;

	ind = left;
	for (i = left; i <= right; i++) {
		if (a[i] < pv) {
			tmp = a[ind];
			a[ind] = a[i];
			a[i] = tmp;
			++ind;
		}
	}
	tmp = a[right];
	a[right] = a[ind];
	a[ind] = tmp;

	return ind;
}

static double selectmedian(double *a, int k, int left, int right)
{
	int pi;
	int ind;

	while (1) {
		pi = left + (int)((right - left)*rand()/(RAND_MAX+1.));
		ind = partition(a, left, right, pi);
		if (ind == k)
			return a[k];
		else if (ind < k)
			left = ind + 1;
		else
			right = ind - 1;
	}
}

/* 
 * Median algo is based on selection algorithm by Blum et. al. Psuedo-code is
 * availble at: http://en.wikipedia.org/wiki/Selection_algorithm
 */
double median(double *v, int N)
{
	int mindx;
	double median;
	double *a;
	
	a = Malloc(sizeof(*a)*N);
	if (!a)
		osd_error_fatal("Out of memory");

	memcpy(a, v, sizeof(*a)*N);
	osd_srand();

	if (N == 0)
		return 0.;
	if ((N%2) == 1) {
		mindx = (N+1)/2 - 1;
		median = selectmedian(a, mindx, 0, N-1);
	} else {
		double m1, m2;
		mindx = N/2;
		m1 = selectmedian(a, mindx, 0, N-1);
		mindx = N/2 - 1;
		m2 = selectmedian(a, mindx, 0, N-1);
		median = (m1 + m2)/2.;
	}

	free(a);
	return median;
}

double get_mhz(void)
{
	FILE *fp;
	char s[1024];
	double mhz = 0;
	static const double cpufrequency_not_found = 1717.17;
#if defined(__FreeBSD__) || defined(__APPLE__)
#ifdef __APPLE__
	#define hw_cpufrequency "hw.cpufrequency"
	const long div_by = 1000000L;
#else
	#define hw_cpufrequency "dev.cpu.0.freq"
	const long div_by = 1L;
#endif

	if (!(fp = popen("sysctl -n " hw_cpufrequency, "r"))) {
		osd_error("Cannot call sysctl");
		return cpufrequency_not_found;
	}

	if (fgets(s, sizeof(s), fp) == NULL ||
	    sscanf(s, "%lf", &mhz) != 1) {
		osd_error("got no hw.cpufrequency sysctl value");
		goto no_cpufrequency;
	}
	mhz /= div_by;

no_cpufrequency:
	pclose(fp);
#else /* defined(__FreeBSD__) || defined(__APPLE__) .e.g Linux */
	char *cp;
	int found = 0;

	if (!(fp = fopen("/proc/cpuinfo", "r"))) {
		osd_error("Cannot open /proc/cpuinfo");
		return cpufrequency_not_found;
	}

	while (fgets(s, sizeof(s), fp)) {
		if (!strncmp(s, "cpu MHz", 7)) {
			found = 1;
			for (cp=s; *cp && *cp != ':'; cp++) ;
			if (!*cp) {
				osd_error("no colon found in string");
				goto no_cpufrequency;
			}
			++cp;
			if (sscanf(cp, "%lf", &mhz) != 1) {
				osd_error("scanf got no value");
				goto no_cpufrequency;
			}
		}
	}
	if (!found)
		osd_error("\"cpu MHz\" line not found\n");

no_cpufrequency:
	fclose(fp); 
#endif /*  else defined(__FreeBSD__) || defined(__APPLE__) */

	return mhz != 0 ? mhz : cpufrequency_not_found ;
}

/*
 * Jenkins One-at-a-time hash.
 * http://en.wikipedia.org/wiki/Hash_table
 * http://www.burtleburtle.net/bob/hash/doobs.html
 */
uint32_t jenkins_one_at_a_time_hash(uint8_t *key, size_t key_len)
{
	size_t i;
	uint32_t hash = 0;

	for (i = 0; i < key_len; i++) {
		hash += key[i];
		hash += (hash << 10);
		hash ^= (hash >> 6);
	}

	hash += (hash << 3);
	hash ^= (hash >> 11);
	hash += (hash << 15);

	return hash;
}

