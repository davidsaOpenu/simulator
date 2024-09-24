/*
 * OSD emulator backing store
 *
 * Copyright (C) 2007 Pete Wyckoff <pw@osc.edu>
 * Copyright (C) 2007 Ananth Devulapalli <ananth@osc.edu>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "list.h"
#include "tgtd.h"

/* osd-target include */
#include "cdb.h"

#ifdef OSDTHREAD
struct bs_threaded_osdemu_private {
	struct osd_device *osd;
	pthread_t osd_thread;
	int cdb_fd[2];
	int sense_fd[2];
};

struct bs_osd_task {
	struct scsi_cmd *cmd;
	int result;
};
#else
struct bs_osdemu_private {
	struct osd_device *osd;
};
#endif

#ifndef OSDTHREAD
/*
 * Initialize private data area that holds the struct osd_device.
 */
static int bs_osdemu_open(struct scsi_lu *lu, char *path, int *fd,
			  uint64_t *size)
{
	struct bs_osdemu_private *priv = (void *) (lu + 1);
	struct osd_device *osd;
	int ret;

	osd = osd_device_alloc();
	if (!osd) {
		ret = -ENOMEM;
		eprintf("osd_device_alloc failed\n");
		goto out;
	}
	priv->osd = osd;
	ret = osd_open(path, osd);
	if (ret) {
		eprintf("osd_open failed\n");
		goto out;
	}
	if (lu->osdname) {
		ret = osd_set_name(osd, lu->osdname);
		if (ret) {
			eprintf("osd_set_name failed\n");
			goto out;
		}
	}

	*fd = -1;
	*size = 0;  /* disk size */
	ret = 0;

out:
	return ret;
}

static void bs_osdemu_close(struct scsi_lu *lu)
{
	struct bs_osdemu_private *priv = (void *) (lu + 1);
	struct osd_device *osd = priv->osd;
	int ret;

	ret = osd_close(osd);
	if (ret)
		eprintf("osd_close failed\n");
	osd_device_free(osd);
}

static int bs_osdemu_cmd_submit(struct scsi_cmd *cmd)
{
	struct bs_osdemu_private *priv = (void *) (cmd->dev + 1);
	struct osd_device *osd = priv->osd;
	uint8_t *data_in, *data_out;
	uint64_t data_in_len, data_out_len;
	int ret;

	/*
	 * iscsi allocs a second buffer for the read result in case of bidi
	 * command.  For normal read/write, one of these directions will
	 * be NULL.
	 */
	data_out = scsi_get_out_buffer(cmd);
	data_out_len = scsi_get_out_length(cmd);
	data_in = scsi_get_in_buffer(cmd);
	data_in_len = scsi_get_in_length(cmd);
	ret = osdemu_cmd_submit(osd, cmd->scb, data_out, data_out_len,
				&data_in, &data_in_len,
				cmd->sense_buffer, &cmd->sense_len);

	/* possible read results on data_in, never underflow on data_out */
	scsi_set_in_resid_by_actual(cmd, data_in_len);
	scsi_set_out_resid_by_actual(cmd, data_out_len);
	return ret;
}

static int bs_osdemu_cmd_done(struct scsi_cmd *cmd)
{
	return 0;
}
#endif  /* !OSDTHREAD */

#ifdef OSDTHREAD
static void bs_sense_handler(int fd, int events, void *data)
{
	int ret;
	struct bs_threaded_osdemu_private *priv = data;
	struct bs_osd_task task;

	ret = read(priv->sense_fd[0], &task, sizeof(task));
	if (ret < 0) {
		eprintf("nothing to read\n");
		return;
	}
	if (ret != sizeof(task)) {
		eprintf("short read\n");
		return;
	}

	dprintf("cmd %p res %d\n", task.cmd, task.result);
	target_cmd_io_done(task.cmd, task.result);
}

static void *bs_cdb_submit_thread(void *arg)
{
	int ret;
	struct bs_threaded_osdemu_private *priv = arg;
	struct osd_device *osd = priv->osd;
	struct bs_osd_task task;
	struct scsi_cmd *cmd;
	uint8_t *data_in, *data_out;
	uint64_t data_in_len, data_out_len;

	for (;;) {
		ret = read(priv->cdb_fd[0], &cmd, sizeof(cmd));
		if (ret < 0) {
			eprintf("unsuccessful read\n");
			if (errno == EAGAIN || errno == EINTR)
				continue;
			break;
		}
		if (ret != sizeof(cmd)) {
			eprintf("short read\n");
			break;
		}

		dprintf("cmd %p\n", task.cmd);

		data_out = scsi_get_out_buffer(cmd);
		data_out_len = scsi_get_out_length(cmd);
		data_in = scsi_get_in_buffer(cmd);
		data_in_len = scsi_get_in_length(cmd);
		ret = osdemu_cmd_submit(osd, cmd->scb, data_out, data_out_len,
					&data_in, &data_in_len,
					cmd->sense_buffer, &cmd->sense_len);

		/* possible read results on data_in, never underflow on
		 * data_out */
		scsi_set_in_resid_by_actual(cmd, data_in_len);
		scsi_set_out_resid_by_actual(cmd, data_out_len);

		task.cmd = cmd;
		task.result = ret;
write_result:
		ret = write(priv->sense_fd[1], &task, sizeof(task));
		if (ret < 0) {
			eprintf("write failed for cmd %p\n", task.cmd);
			if (errno == EAGAIN || errno == EINTR)
				goto write_result;
			break;
		}
		if (ret != sizeof(task)) {
			eprintf("short write\n");
			break;
		}
	}

	return NULL;
}

/*
 * Initialize private data area that holds the struct osd_device. Spawn off
 * osd thread
 */
static int bs_threaded_osdemu_open(struct scsi_lu *lu, char *path, int *fd,
				   uint64_t *size)
{
	int ret;
	struct bs_threaded_osdemu_private *priv = (void *) (lu + 1);
	struct osd_device *osd;

	osd = osd_device_alloc();
	if (!osd) {
		ret = -ENOMEM;
		eprintf("osdemu_device_alloc failed\n");
		goto out;
	}
	priv->osd = osd;
	ret = osd_open(path, osd);
	if (ret) {
		eprintf("osd_open failed\n");
		goto out;
	}

	ret = pipe(priv->cdb_fd);
	if (ret < 0)
		goto close_osd;

	ret = pipe(priv->sense_fd);
	if (ret < 0)
		goto close_cdb_fd;

	ret = tgt_event_add(priv->sense_fd[0], EPOLLIN, bs_sense_handler, priv);
	if (ret)
		goto close_sense_fd;

	ret = pthread_create(&priv->osd_thread, NULL, bs_cdb_submit_thread,
			     priv);
	if (ret)
		goto event_del;

	*fd = -1;
	*size = 0;  /* disk size */
	return 0;

event_del:
	tgt_event_del(priv->sense_fd[0]);
close_sense_fd:
	close(priv->sense_fd[0]);
	close(priv->sense_fd[1]);
close_cdb_fd:
	close(priv->cdb_fd[0]);
	close(priv->cdb_fd[1]);
close_osd:
	osd_close(osd);
	osd_device_free(osd);
out:
	return ret;
}

static void bs_threaded_osdemu_close(struct scsi_lu *lu)
{
	struct bs_threaded_osdemu_private *priv = (void *) (lu + 1);
	struct osd_device *osd = priv->osd;
	int ret;

	pthread_cancel(priv->osd_thread);
	pthread_join(priv->osd_thread, NULL);

	tgt_event_del(priv->sense_fd[0]);

	close(priv->sense_fd[0]);
	close(priv->sense_fd[1]);
	close(priv->cdb_fd[0]);
	close(priv->cdb_fd[1]);

	ret = osd_close(osd);
	if (ret)
		eprintf("osd_close failed\n");
	osd_device_free(osd);
}

static int bs_threaded_osdemu_cmd_submit(struct scsi_cmd *cmd)
{
	int ret;
	struct bs_threaded_osdemu_private *priv = (void *) (cmd->dev + 1);

write_cdb:
	ret = write(priv->cdb_fd[1], &cmd, sizeof(cmd));
	if (ret < 0) {
		eprintf("write failed for cmd %p\n", cmd);
		if (errno == EAGAIN || errno == EINTR)
			goto write_cdb;
		goto out;
	}
	if (ret != sizeof(cmd)) {
		eprintf("short write\n");
		ret = -EIO;
		goto out;
	}

	dprintf("cmd %p\n", cmd);
	/*
	 * setting async without lock is safe since osd_thread ignores that
	 * field
	 */
	set_cmd_async(cmd);
	ret = 0;
out:
	return ret;
}

static int bs_threaded_osdemu_cmd_done(struct scsi_cmd *cmd)
{
	return 0;
}
#endif  /* OSDTHREAD */

#ifdef OSDTHREAD
static struct backingstore_template osdemu_bst = {
	.bs_name	= "osdemu",
	.bs_datasize	= sizeof(struct bs_threaded_osdemu_private),
	.bs_open	= bs_threaded_osdemu_open,
	.bs_close	= bs_threaded_osdemu_close,
	.bs_cmd_submit	= bs_threaded_osdemu_cmd_submit,
	.bs_cmd_done	= bs_threaded_osdemu_cmd_done,
};
#else
static struct backingstore_template osdemu_bst = {
	.bs_name	= "osdemu",
	.bs_datasize	= sizeof(struct bs_osdemu_private),
	.bs_open	= bs_osdemu_open,
	.bs_close	= bs_osdemu_close,
	.bs_cmd_submit	= bs_osdemu_cmd_submit,
	.bs_cmd_done	= bs_osdemu_cmd_done,
};
#endif

__attribute__((constructor)) static void bs_osdemu_constructor(void)
{
	register_backingstore_template(&osdemu_bst);
}
