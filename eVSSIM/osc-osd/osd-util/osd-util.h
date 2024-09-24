/*
 * Declarations of osd-util.c utilities.
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
#ifndef __OSD_UTIL_H
#define __OSD_UTIL_H

#include <stdint.h>
#include <stdlib.h>
#include <endian.h>
#include <time.h>

#if defined(__x86_64__)
	#define rdtsc(v) do { \
        	unsigned int __a, __d; \
        	asm volatile("rdtsc" : "=a" (__a), "=d" (__d)); \
        	(v) = ((unsigned long)__a) | (((unsigned long)__d)<<32); \
	} while(0)
#elif defined(__i386__)
	#define rdtsc(v) do { \
		asm volatile("rdtsc" : "=A" (v)); \
	} while (0)
#endif

extern double mhz;
void osd_set_progname(int argc, char *const argv[]);
const char *osd_get_progname(void);
void osd_info(const char *fmt, ...) __attribute__((format(printf,1,2)));
void osd_warning(const char *fmt, ...) __attribute__((format(printf,1,2)));
void osd_error(const char *fmt, ...) __attribute__((format(printf,1,2)));
void osd_error_errno(const char *fmt, ...) __attribute__((format(printf,1,2)));
void osd_error_xerrno(int errnum, const char *fmt, ...) __attribute__((format(printf,2,3)));
void osd_error_fatal(const char *fmt, ...) __attribute__((format(printf,1,2)));
void *Malloc(size_t n) __attribute__((malloc));
void *Calloc(size_t nmemb, size_t n) __attribute__((malloc));
size_t osd_saferead(int fd, void *buf, size_t num);
size_t osd_safewrite(int fd, const void *buf, size_t num);
void osd_hexdump(const void *dv, size_t len);
double mean(double *v, int N);
double stddev(double *v, double mu, int N);
double median(double *v, int N);
double get_mhz(void);
uint32_t jenkins_one_at_a_time_hash(uint8_t *key, size_t key_len);

/*
 * Disable debugging with -DNDEBUG in CFLAGS.  This also disables assert().
 */
#ifndef NDEBUG
#define osd_debug(fmt,args...) \
	do { \
		osd_info(fmt,##args); \
	} while (0)
#else
#define osd_debug(fmt,...) do { } while (0)
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (int)(sizeof(x) / sizeof((x)[0]))
#endif

#ifndef llu
#define llu(x) ((unsigned long long) (x))
#endif

/* endian covertors */
uint16_t get_ntohs_le(const void *d);
uint16_t get_ntohs_be(const void *d);
uint32_t get_ntohl_le(const void *d);
uint32_t get_ntohl_be(const void *d);
uint64_t get_ntohll_le(const void *d);
uint64_t get_ntohll_be(const void *d);
uint64_t get_ntohtime_le(const void *d);
uint64_t get_ntohtime_be(const void *d);
void set_htons_le(void *x, uint16_t val);
void set_htons_be(void *x, uint16_t val);
void set_htonl_le(void *x, uint32_t val);
void set_htonl_be(void *x, uint32_t val);
void set_htonll_le(void *x, uint64_t val);
void set_htonll_be(void *x, uint64_t val);
void set_htontime_le(void *x, uint64_t val);
void set_htontime_be(void *x, uint64_t val);

uint64_t get_ntohoffset(const void *d);
void set_htonoffset(void *x, uint64_t val);
uint64_t next_offset(uint64_t start);

/* remove netdb.h declarations */
#undef ntohs
#undef ntohl

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define get_ntohs      get_ntohs_le
#define get_ntohl      get_ntohl_le
#define get_ntohll     get_ntohll_le
#define get_ntohtime   get_ntohtime_le
#define set_htons      set_htons_le
#define set_htonl      set_htonl_le
#define set_htonll     set_htonll_le
#define set_htontime   set_htontime_le
#else
#define get_ntohs      get_ntohs_be
#define get_ntohl      get_ntohl_be
#define get_ntohll     get_ntohll_be
#define get_ntohtime   get_ntohtime_be
#define set_htons      set_htons_be
#define set_htonl      set_htonl_be
#define set_htonll     set_htonll_be
#define set_htontime   set_htontime_be
#endif

/* round up an integer to the next multiple of 8 */
#ifndef roundup8
#define roundup8(x) (((x) + 7) & ~7)
#endif

static inline void osd_srand(void) 
{
	srand(time(0));
}

#endif /* __OSD_UTIL_H */
