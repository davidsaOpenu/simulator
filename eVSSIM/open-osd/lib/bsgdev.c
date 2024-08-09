/*
 *  C Implementation: bsgdev.c
 *
 * Description: This file emulates Linux blk-device API in user-mode by means
 *              of the bsg.ko kernel module using an sg-v4 SG_IO ioctl.
 *              Also emulated a basic functionality of a bio structure and API
 *
 * Author: Boaz Harrosh <bharrosh@panasas.com>, (C) 2009
 *
 * Copyright: See COPYING file that comes with this distribution
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. Neither the name of the Panasas company nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/ioctl.h>

#include <linux/blkdev.h>
#include <linux/bsg.h>
#include <scsi/sg.h>

static int _queue_num_requests(struct request_queue* q);
static int _bsg_wait_response(struct request_queue* q);

#ifdef CONFIG_SCSI_OSD_DEBUG
#  define bsg_dbg printf
#else
#  define bsg_dbg(msg, a...) do{}while(0)
#endif
/*
 * Error.
 */
static void __attribute__((format(printf,1,2)))
bsg_error(const char *fmt, ...)
{
	va_list ap;

	fprintf(stderr, "bsg: Error: ");
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, ".\n");
}

/*
 * Error with the errno message.
 */
static void __attribute__((format(printf,1,2)))
bsg_error_errno(const char *fmt, ...)
{
	va_list ap;

	fprintf(stderr, "bsg: Error: ");
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, ": %s.\n", strerror(errno));
}

int __g_using_iovec = 0;

int bsg_open(struct request_queue *q, const char *bsg_path)
{
	int ret = 0;

	q->fd = open(bsg_path, O_RDWR);
	if (q->fd < 0)
		ret = errno;

	bsg_dbg("bsg_open(%s) => (%d, %d)\n", bsg_path, q->fd, ret);
	return ret;
}

void bsg_close(struct request_queue* q)
{
	bsg_dbg("bsg_close(%d)\n", q->fd);

	if (q->fd < 0)
		return;

	while(_queue_num_requests(q))
		_bsg_wait_response(q);

	close(q->fd);
	q->fd = -1;
}

static inline void _queue_inc_requests(struct request_queue* q)
{
	++q->num_requests;
}

static inline void _queue_dec_requests(struct request_queue* q)
{
	if (q->num_requests > 0)
		--q->num_requests;
	else
		bsg_error("_queue_dec_requests counter below zero");
}

static int _queue_num_requests(struct request_queue* q)
{
	return q->num_requests;
}

static struct bio *__new_bio(unsigned max_vecs)
{
	struct bio *bio = kzalloc(sizeof(struct bio) +
				 sizeof(struct biovec) * max_vecs, 0);

	if (!bio)
		return NULL;

	bio->ref = 1;
	bio->bi_max_vecs = max_vecs;
	return bio;
}

void bio_put(struct bio *bio)
{
	if (!--bio->ref)
		kfree(bio);
}

void bio_endio(struct bio *bio, int error __unused)
{
	bio_put(bio);
}

struct bio *bio_kmalloc(gfp_t gfp_mask __unused, int nr_iovecs)
{
	return __new_bio(nr_iovecs);
}

int bio_add_pc_page(struct request_queue *q __unused,
		    struct bio *bio, struct page *page, unsigned len,
		    unsigned offset __unused)
{
	if (bio->bi_max_vecs >= bio->bi_vecs)
		return 0;

	bio->bi_vec[bio->bi_vecs].mem = page;
	bio->bi_vec[bio->bi_vecs].len = len;
	bio->bi_size += len;

	bio->bi_vecs++;
}

struct bio *bio_map_kern(struct request_queue *q __unused,
			 void *data, unsigned int len, gfp_t gfp __unused)
{
	struct bio *bio = __new_bio(1);

	if (!bio)
		return NULL;

	bio->bi_vec[0].mem = data;
	bio->bi_vec[0].len = len;
	bio->bi_size = len;

	bio->bi_vecs = 1;
	return bio;
}

static int __bio_append(struct bio *first, struct bio *bio)
{
	first->bi_size += bio->bi_size;
	while(first->bi_next)
		first = first->bi_next;

	first->bi_next = bio;
	return 0;
}

static inline int _rq_is_write(struct request *rq)
{
	return rq->next_rq ? 1 : rq->rw;
}

static unsigned _count_vects(struct bio *bio, unsigned *vec_num)
{
	unsigned total_len = 0, vn = 0;
	unsigned i;

	for (; bio; bio = bio->bi_next) {
		for (i=0; i < bio->bi_vecs; i++) {
			++vn;
			total_len += bio->bi_vec[i].len;
		}
	}

	if (vec_num)
		*vec_num = vn;
	return total_len;
}

static void copy_bios_to_iovec(struct bio *bio, struct sg_iovec *vects)
{
	unsigned i, v = 0;

	for (; bio; bio = bio->bi_next) {
		for (i=0; i < bio->bi_vecs; i++, v++) {
			vects[v].iov_base = bio->bi_vec[i].mem;
			vects[v].iov_len = bio->bi_vec[i].len;
		}
	}
}

static void copy_bios_to_buff(struct bio *bio, void *buff)
{
	unsigned i;

	for (; bio; bio = bio->bi_next) {
		for (i=0; i < bio->bi_vecs; i++) {
			struct biovec *bv = &bio->bi_vec[i];

			memcpy(buff, bv->mem , bv->len);
			bsg_dbg("copy_bios_to_buff %p, %p, %zu\n",
				  buff, bv->mem, bv->len);
			buff += bv->len;
		}
	}
}

static void copy_buff_to_bios(void *buff, struct bio *bio)
{
	unsigned i;

	for (; bio; bio = bio->bi_next) {
		for (i=0; i < bio->bi_vecs; i++) {
			struct biovec *bv = &bio->bi_vec[i];

			memcpy(bv->mem, buff, bv->len);
			bsg_dbg("copy_buff_to_bios %p, %p, %zu\n",
				  bv->mem, buff, bv->len);
			buff += bv->len;
		}
	}
}

/* must be called before total_len & iovec_count */
static inline void *_rq_buff(struct request *rq)
{
	rq->__data_len = _count_vects(rq->bio, &rq->iovec_num);

	if (!rq->__data_len)
		return NULL;

	if(__g_using_iovec) {
		rq->iovec = kalloc(rq->iovec_num * sizeof(struct sg_iovec), 0);
		if (!rq->iovec)
			return NULL;/*ERR_PTR(-ENOMEM)*/

		copy_bios_to_iovec(rq->bio, rq->iovec);
		bsg_dbg("__g_using_iovec %u\n", rq->iovec_num);
		return rq->iovec;
	} else if(rq->iovec_num == 1){
		rq->bounce = rq->bio->bi_vec[0].mem;
		rq->iovec_num = 0;
		bsg_dbg("rq->iovec_num == 1 %p %u\n", rq->bounce, rq->__data_len);
	} else {
		rq->bounce = kalloc(rq->__data_len, 0);
		if (_rq_is_write(rq))
			copy_bios_to_buff(rq->bio, rq->bounce);
		bsg_dbg("bounce %p %u\n", rq->bounce, rq->__data_len);
	}
	return rq->bounce;
}

static inline unsigned _rq_total_len(struct request *rq)
{
	return rq->__data_len;
}

static inline unsigned _rq_iovec_count(struct request *rq)
{
	return rq->bounce ? 0 : rq->iovec_num;
}

struct request *blk_get_request(struct request_queue *q, int rw, gfp_t gfp)
{
	struct request * rq = kzalloc(sizeof(struct request), gfp);

	// ref counting??
	rq->q = q;
	rq->rw = rw;
	return rq;
}

static void _blk_end_request(struct request *rq, int error)
{
	struct bio *bio;

	if (rq->next_rq)
		_blk_end_request(rq->next_rq, error);

	if(rq->bounce && rq->iovec_num) {
		if (!_rq_is_write(rq) && !error)
			copy_buff_to_bios(rq->bounce, rq->bio);
		kfree(rq->bounce);
	}
	rq->bounce = NULL;

	kfree(rq->iovec);
	rq->iovec = NULL;

	while ((bio = rq->bio) != NULL) {
		rq->bio = bio->bi_next;
		bio_endio(bio, 0);
	}
}

int blk_end_request(struct request *rq, int error,
		    unsigned int nr_bytes __unused)
{
	_blk_end_request(rq, error);
	return 0;
}

void blk_put_request(struct request *rq)
{
	// ref counting??
	kfree(rq);
}

static int blk_rq_append_bio(struct request_queue *q __unused,
			     struct request *rq, struct bio *bio)
{
	int ret;

	if (rq->rw)
		bio->bi_rw |= REQ_WRITE;
	else
		bio->bi_rw &= ~REQ_WRITE;
	
	if (!rq->bio) {
		rq->bio = bio;
		ret = 0;
	} else
		ret = __bio_append(rq->bio, bio);

	rq->__data_len = rq->bio->bi_size;
	return ret;
}

struct request *blk_make_request(struct request_queue *q, struct bio *bio,
				 gfp_t gfp_mask)
{
	struct request *rq = blk_get_request(q, bio->bi_rw & REQ_WRITE,
					     gfp_mask);

	if (rq) {
		blk_rq_append_bio(q, rq, bio);
	}
	return rq;
}

int blk_rq_map_kern(struct request_queue *q, struct request *rq, void *kbuf,
		    unsigned int len, gfp_t gfp_mask)
{
	struct bio *bio = bio_map_kern(q, kbuf, len, gfp_mask);
	if (!bio)
		return -ENOMEM;

	return blk_rq_append_bio(q, rq, bio);
}

static void __end_io(struct request *rq, struct sg_io_v4 *sg)
{
	_blk_end_request(rq, 0);

	if (_rq_is_write(rq)) {
		rq->resid_len = sg->dout_resid;
		if (rq->next_rq)
			rq->next_rq->resid_len = sg->din_resid;
	} else
		rq->resid_len = sg->din_resid;

	rq->errors = (sg->device_status << 1) | (sg->transport_status << 16) |
			(sg->driver_status << 24);
	rq->sense_len = sg->response_len;
}

static int _bsg_submit_request(struct request_queue *q, struct request *rq,
				bool sync)
{
	int ret;
	struct sg_io_v4 sg;
	struct request *read_rq = NULL;

	memset(&sg, 0, sizeof(sg));
	sg.guard = 'Q';
	sg.request_len = rq->cmd_len;
	sg.request = (uint64_t) (unsigned long) rq->cmd;
	sg.max_response_len = 260;
	sg.response = (uint64_t) (unsigned long) rq->sense;

	if (_rq_is_write(rq)) {
		sg.dout_xferp = (uint64_t) (unsigned long) _rq_buff(rq);
		sg.dout_xfer_len = _rq_total_len(rq);
		sg.dout_iovec_count = _rq_iovec_count(rq);
		read_rq = rq->next_rq;
		bsg_dbg("Has write(%p %u %u %p)\n",
			(void*)sg.dout_xferp, sg.dout_xfer_len,
			sg.dout_iovec_count, read_rq);
	} else if(rq->bio)
		read_rq = rq;

	if(read_rq) {
		sg.din_xferp = (uint64_t) (unsigned long)_rq_buff(read_rq);
		sg.din_xfer_len = _rq_total_len(read_rq);
		sg.din_iovec_count = _rq_iovec_count(read_rq);
		bsg_dbg("Has read(%p %u %u)\n",
			(void*)sg.din_xferp, sg.din_xfer_len,
			sg.din_iovec_count);
	}


	sg.timeout = rq->timeout * 1000 / HZ;
/*	rq->timeout = (hdr->timeout * HZ) / 1000;
*/
	rq->q = q;
	sg.usr_ptr = (uint64_t) (unsigned long)rq;
	sg.flags = BSG_FLAG_Q_AT_TAIL;

	if (sync){
		ret = ioctl(q->fd, SG_IO, &sg);
		__end_io(rq, &sg);
		bsg_dbg("ioctl(%d,SG_IO) => %d\n", q->fd, ret);
		if (likely(!ret && !rq->errors))
			return 0;
		goto error;
	}

	/* Perform an async run */
	_queue_inc_requests(q);
	ret = write(q->fd, &sg, sizeof(sg));
	if (likely(ret == sizeof(sg)))
		return 0;

	/* Failed! request not submitted */
	_queue_dec_requests(q);

error:
	if (!ret && rq->errors) {
		errno = EIO; ret = -EIO;
	}

	if (ret < 0) {
		ret = -errno;
		if (ret == -EOPNOTSUPP)
			bsg_error(
			    "bsg: Error: Device (fd=%d) does not support BIDI",
			    q->fd);
		else
			bsg_dbg("%s: %s\n", sync ? "ioctl" : "write",
				strerror(errno));
	} else {
		bsg_error("Short write, %d not %zu", ret, sizeof(sg));
		ret = -EIO;
	}
	return ret;
}

static int _bsg_wait_response(struct request_queue *q)
{
	struct sg_io_v4 sg;
	struct request *rq;
	int ret;

	ret = read(q->fd, &sg, sizeof(sg));
	if (ret < 0) {
		bsg_error_errno("%s: read", __func__);
		return -errno;
	}
	if (ret != sizeof(sg)) {
		bsg_error("%s: short read, %d not %zu", __func__, ret,
		          sizeof(sg));
		return -EPIPE;
	}

	rq = (void *)(unsigned long) sg.usr_ptr;
	_queue_dec_requests(rq->q);
	__end_io(rq, &sg);

	if (rq->rq_end_io)
		rq->rq_end_io(rq, sg.device_status);
	else
		blk_put_request(rq);

	return 0;
}

/*
int bsg_wait_response(struct request_queue *q)
{
	int ret;

	while(_queue_num_requests(q)) {
		ret = _bsg_wait_response(q->fd);
		if (ret)
			return ret;
	}

	return 0;
}
*/

int blk_execute_rq(struct request_queue *q __unused,
		   void *bd_disk_unused __unused, struct request *rq,
		   int at_head __unused)
{
	return _bsg_submit_request(q, rq, 1);
}

void blk_execute_rq_nowait(struct request_queue *q __unused,
			   void *bd_disk_unused __unused, struct request *rq,
			   int at_head __unused, rq_end_io_fn *done)
{
	rq->rq_end_io = done;
	_bsg_submit_request(q, rq, 0);
}
