/*
 * kalloc.h Some Kernel API emulation for user-space
 *
 * Description: Mainly allocation stuff and more
 *
 * Author: Boaz Harrosh <bharrosh@panasas.com>, (C) 2009
 *
 * Copyright: See COPYING file that comes with this distribution
 */

#ifndef __KinU_KALLOC_H__
#define __KinU_KALLOC_H__

#include <linux/types.h>

typedef int gfp_t;
#define GFP_KERNEL 0

void *kalloc(size_t size, gfp_t unused);
void *kzalloc(size_t size, gfp_t unused);
void *krealloc(const void *p, size_t new_size, gfp_t flags);
void kfree(const void *objp); /*NULL safe like Kernel*/

unsigned long __get_free_page(gfp_t unused);
void free_page(unsigned long addr);

/* hexdump.c */
extern const char hex_asc[];
#define hex_asc_lo(x)	hex_asc[((x) & 0x0f)]
#define hex_asc_hi(x)	hex_asc[((x) & 0xf0) >> 4]

static inline char *pack_hex_byte(char *buf, u8 byte)
{
	*buf++ = hex_asc_hi(byte);
	*buf++ = hex_asc_lo(byte);
	return buf;
}
void hex_dump_to_buffer(const void *buf, size_t len, size_t rowsize,
			size_t groupsize, char *linebuf, size_t linebuflen,
			bool ascii);

#endif /* ndef __LinUM_KALLOC_H__ */
