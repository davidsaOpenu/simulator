/*
 * User-mode emulation of <linux/blkdev.h>
 *
 * Description: User-mode emulation of the request, request_queue, and biovec
 *              APIs from kernel. For compilation of osd_xxx Kernel source files
 *              in user-mode. Implementation is in bsgdev.c file.
 *
 * Author: Boaz Harrosh <bharrosh@panasas.com>, (C) 2009
 *
 * Copyright: See COPYING file that comes with this distribution
 *
 */
#ifndef __KinU_BLKDEV_H__
#define __KinU_BLKDEV_H__

#include <linux/types.h>
#include <asm/unaligned.h>
#include <asm/param.h>
#include "kalloc.h"
#include "err.h"

#include <string.h>
#include <errno.h>
#include <unistd.h>

#define BLK_DEFAULT_SG_TIMEOUT	(60 * HZ)

struct request_queue {
	int fd;
	volatile int num_requests;
};

int bsg_open(struct request_queue *q, const char *bsg_path);
void bsg_close(struct request_queue* q);

struct bio {
	int ref;
	struct bio	*bi_next;
	size_t		bi_size;
	u32		bi_rw;
	unsigned bi_vecs;
	unsigned bi_max_vecs;
	struct biovec {
		void* mem;
		size_t len;
	} bi_vec[];
};

enum {
	REQ_WRITE = 1,
	REQ_TYPE_BLOCK_PC = 1,
	READ = 0,
	WRITE = 1,
};

/* For user-mode we make it look like everything is a single page */
struct page *page;
#define PAGE_SIZE ~0
#define offset_in_page(p) 0
#define virt_to_page(p) ((struct page *)p)

void bio_put(struct bio *bio);
void bio_endio(struct bio *bio, int error);
struct bio *bio_kmalloc(gfp_t gfp_mask, int nr_iovecs);
int bio_add_pc_page(struct request_queue *q, struct bio *bio, struct page *page,
		    unsigned int len, unsigned int offset);
struct bio *bio_map_kern(
	struct request_queue *q, void *data, unsigned int len,
			 gfp_t gfp_mask);

struct request;
typedef void (rq_end_io_fn)(struct request *, int);

struct request {
	struct request_queue *q;
	struct bio *bio;
	struct request *next_rq;

	int rw;
	int cmd_type;
	int cmd_flags;
	u8 *cmd;
	unsigned cmd_len;
	unsigned int timeout;
	int retries;

	unsigned int __data_len;
	unsigned int iovec_num;
	void *iovec; /* sg_iovec */
	void *bounce; /* linear buffer in case sg_iovec is not supported */

	int errors;
	unsigned resid_len;
	void *sense;
	unsigned sense_len;

	void* end_io_data;
	rq_end_io_fn *rq_end_io;
};

#define REQ_QUIET	(1 << 2)

struct request *blk_get_request(struct request_queue *q, int rw, gfp_t gfp);
void blk_put_request(struct request *req);
static inline void __blk_put_request(struct request_queue *q __unused,
				     struct request *req)
{
	blk_put_request(req);
}

static inline unsigned int blk_rq_bytes(struct request *rq)
{
	return rq->__data_len;
}

int blk_end_request(struct request *rq, int error, unsigned int nr_bytes);
struct request *blk_make_request(struct request_queue *q, struct bio *bio,
				 gfp_t gfp_mask);
int blk_rq_map_kern(struct request_queue *q, struct request *rq, void *kbuf,
		    unsigned int len, gfp_t gfp_mask);
int blk_execute_rq(struct request_queue *q, void *bd_disk_unused,
		   struct request *rq, int at_head);
void blk_execute_rq_nowait(struct request_queue *q, void *bd_disk_unused,
			   struct request *rq, int at_head, rq_end_io_fn *done);

#endif /* __KinU_BLKDEV_H__ */
