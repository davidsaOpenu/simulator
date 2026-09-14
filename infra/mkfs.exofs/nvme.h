#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <linux/types.h>

struct nvme {
    int fd;
};

#define NVME_INIT() (struct nvme){-1}

int nvme_open(struct nvme *nvme, const char *path);
void nvme_close(struct nvme *nvme);
ssize_t nvme_obj_write(struct nvme *nvme, const uint64_t id, const void *value, size_t len, off_t offset);
ssize_t nvme_obj_read(struct nvme *nvme, const uint64_t id, void *value, size_t len, off_t offset);
