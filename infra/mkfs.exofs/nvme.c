#include "nvme.h"
#include "debug.h"
#include "uapi/linux/nvme_ioctl.h"
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/ioctl.h>

int nvme_open(struct nvme *nvme, const char *path)
{
    const int fd = open(path, O_RDWR);
    if (fd == -1)
    {
        return -errno;
    }

    nvme->fd = fd;
    return 0;
}

void nvme_close(struct nvme *nvme)
{
    close(nvme->fd);
    *nvme = NVME_INIT();
}

ssize_t nvme_obj_write(struct nvme *nvme, const uint64_t id, const void *value, size_t len, off_t offset)
{
    struct nvme_user_obj_io obj_io = {
        .opcode = nvme_kv_store,
        .offset = offset,
        .addr = (__u64)value,
        .length = len,
        .key_low = id,
        .key_high = 0,
        .key_len = NVME_OBJ_ID_MAXLEN,
    };

    const ssize_t ret = ioctl(nvme->fd, NVME_IOCTL_SUBMIT_OBJ_IO, &obj_io);
    TRACE("writing %lu bytes to %lu at offset %lu -> %ld,%d\n", len, id, offset, ret, obj_io.length);
    if (ret < 0)
    {
        return -errno;
    }
    else if (ret > 0)
    {
        return -EIO;
    }

    return obj_io.length;
}

ssize_t nvme_obj_read(struct nvme *nvme, const uint64_t id, void *value, size_t len, off_t offset)
{
    struct nvme_user_obj_io obj_io = {
        .opcode = nvme_kv_retrieve,
        .offset = offset,
        .addr = (__u64)value,
        .length = len,
        .key_low = id,
        .key_high = 0,
        .key_len = NVME_OBJ_ID_MAXLEN,
    };
    TRACE("obj_io.length = %d\n", obj_io.length);

    const ssize_t ret = ioctl(nvme->fd, NVME_IOCTL_SUBMIT_OBJ_IO, &obj_io);
    TRACE("reading %lu bytes from %lu -> %ld,%d\n", len, id, ret, obj_io.length);

    if (ret < 0)
    {
        return -errno;
    }
    else if (ret > 0)
    {
        return -EIO;
    }

    return obj_io.length;
}