#!/bin/bash
source ./builder.sh

# Build nvme
evssim_run_at_folder $EVSSIM_NVME_CLI_FOLDER "make clean && make && cp nvme $EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/nvme"

# Build dnvme & tnvme
evssim_run_at_folder $EVSSIM_NVME_COMPLIANCE_FOLDER "cd dnvme && make DIST=$EVSSIM_KERNEL_DIST KDIR=$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/kernel/lib/modules/$EVSSIM_KERNEL_DIST/build && cp dnvme.ko $EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/dnvme.ko"
evssim_run_at_folder $EVSSIM_NVME_COMPLIANCE_FOLDER "cd tnvme && make && cp tnvme $EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/tnvme"

EVSSIM_YABS_REPO=https://github.com/masonr/yet-another-bench-script
EVSSIM_YABS_REF=873f780055372ed00eace2e4050c85a0d0ed8bb1
EVSSIM_YABS_CACHE_DIR=$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/.cache/yet-another-bench-script
EVSSIM_YABS_DIST_DIR=$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/yet-another-bench-script

echo "INFO Staging pinned YABS $EVSSIM_YABS_REF"
evssim_run_at_path $EVSSIM_DOCKER_ROOT_PATH "mkdir -p '$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/.cache' && if [ -d '$EVSSIM_YABS_CACHE_DIR/.git' ] && cd '$EVSSIM_YABS_CACHE_DIR' && git rev-parse HEAD | grep -qx '$EVSSIM_YABS_REF'; then echo 'INFO Reusing pinned YABS $EVSSIM_YABS_REF'; else rm -rf '$EVSSIM_YABS_CACHE_DIR' && git clone -q '$EVSSIM_YABS_REPO' '$EVSSIM_YABS_CACHE_DIR' && cd '$EVSSIM_YABS_CACHE_DIR' && git checkout -q '$EVSSIM_YABS_REF'; fi && cd '$EVSSIM_YABS_CACHE_DIR' && git rev-parse HEAD | grep -qx '$EVSSIM_YABS_REF' && rm -rf '$EVSSIM_YABS_DIST_DIR' && mkdir -p '$EVSSIM_YABS_DIST_DIR' && rsync -a --delete --exclude '.git' '$EVSSIM_YABS_CACHE_DIR/' '$EVSSIM_YABS_DIST_DIR/' && chmod +x '$EVSSIM_YABS_DIST_DIR/yabs.sh'"
