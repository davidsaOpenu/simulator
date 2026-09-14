#define _GNU_SOURCE
#include "exofs.h"
#include "debug.h"
#include "exofs_fs.h"

#include <endian.h>
#include <string.h>
#include <errno.h>
#include <time.h>

static int write_super(struct nvme *nvme);
static int write_root(struct nvme *nvme);
static int write_root_inode(struct nvme *nvme);
static const size_t ROOT_SIZE = 4096;

int mkfs_exofs(struct nvme *nvme)
{
    int err = write_super(nvme);
    if (err)
    {
        TRACE("failed to write super block: %s\n", strerror(-err));
        return err;
    }

    err = write_root(nvme);
    if (err)
    {
        TRACE("failed to write root: %s\n", strerror(-err));
        return err;
    }

    err = write_root_inode(nvme);
    if (err)
    {
        TRACE("failed to write root inode: %s\n", strerror(-err));
        return err;
    }

    return err;
}

static int write_super(struct nvme *nvme)
{
    TRACE("writing super\n");
    struct exofs_fscb super = {
        .s_magic = htole16(EXOFS_SUPER_MAGIC),
        .s_version = htole16(EXOFS_FSCB_VER),
        .s_newfs = htole16(1),
        .s_nextid = htole64(1),
        .s_numfiles = htole64(1),
    };

    const int ret = nvme_obj_write(nvme, EXOFS_SUPER_ID, &super, sizeof(super), 0);
    if (ret < 0)
        return ret;
    return 0;
}

static int write_root(struct nvme *nvme)
{
    TRACE("writing root\n");
    void *buf = malloc(ROOT_SIZE);
    if (buf == NULL)
        return -ENOMEM;

    memset(buf, 0, ROOT_SIZE);

    struct exofs_dir_entry dot = {
        .inode_no = htole64(EXOFS_ROOT_ID),
        .rec_len = htole16(EXOFS_DIR_REC_LEN(1)),
        .name_len = 1,
        .file_type = EXOFS_FT_DIR,
        .name = ".",
    };

    struct exofs_dir_entry dotdot = {
        .inode_no = htole64(EXOFS_ROOT_ID),
        .rec_len = htole16(ROOT_SIZE - le16toh(dot.rec_len)),
        .name_len = 2,
        .file_type = EXOFS_FT_DIR,
        .name = "..",
    };

    memcpy(buf, &dot, sizeof(dot));
    memcpy(buf + le16toh(dot.rec_len), &dotdot, sizeof(dotdot));

    const int ret = nvme_obj_write(nvme, EXOFS_ROOT_ID, buf, ROOT_SIZE, 0);
    if (ret < 0)
        return ret;
    return 0;
}

static int write_root_inode(struct nvme *nvme)
{
    TRACE("writing root inode\n");
    struct timespec now_ts;
    if (clock_gettime(CLOCK_REALTIME, &now_ts))
    {
        return -errno;
    }

    struct exofs_fcb root_inode = {
        .i_size = htole64(ROOT_SIZE),
        .i_links_count = htole16(1),
        .i_mode = htole16(0040000 | (0777 & ~022)),
        .i_atime = htole64(now_ts.tv_sec),
        .i_ctime = htole64(now_ts.tv_sec),
        .i_mtime = htole64(now_ts.tv_sec),
    };

    ssize_t ret = nvme_obj_write(nvme, exofs_nvme_metadata(EXOFS_ROOT_ID), &root_inode, sizeof(root_inode), 0);
    if (ret < 0)
        return ret;
    return 0;
}
