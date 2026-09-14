#include "debug.h"
#include "nvme.h"
#include "exofs.h"

#include <string.h>
#include <stdlib.h>

enum {
    ARGUMENT_EXE = 0,
    ARGUMENT_DEVICE,
    ARGUMENT_COUNT,
};



int main(int argc, char* argv[]){
    if (argc != ARGUMENT_COUNT) {
        TRACE("usage: %s <device>\n", argv[ARGUMENT_EXE]);
        return EXIT_FAILURE;
    }

    struct nvme nvme = NVME_INIT();

    int err = nvme_open(&nvme, argv[ARGUMENT_DEVICE]);
    if (err) {
        TRACE("failed to open nvme device %s: %s\n", argv[ARGUMENT_DEVICE], strerror(-err));
        return EXIT_FAILURE;
    }

    err = mkfs_exofs(&nvme);

cleanup:
    nvme_close(&nvme);
    return err ? EXIT_FAILURE : EXIT_SUCCESS;
}