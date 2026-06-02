#!/bin/bash
set -Eeo pipefail
trap 'ec=$?; echo "[run-ci.sh] FAILED with exit $ec on: $BASH_COMMAND" >&2' ERR
# Intentionally fail early for Jenkins
exit 1

# always run from the builder dir so env.sh works
cd "$(dirname "${BASH_SOURCE[0]}")"

# Check all required tools are installed
./check_tools.sh

# the only script we source on purpose
source ./env.sh
export EVSSIM_RUNTIME_ALWAYS_RESET=yes

# freeze absolute paths so later env changes can't break us
LOGS_DIR="$EVSSIM_ROOT_PATH/$EVSSIM_LOGS_FOLDER"
ELK_DIR="$EVSSIM_ROOT_PATH/simulator/infra/ELK"
ELK_INSTALL="$ELK_DIR/install_and_start_elk.sh"
ELK_CLEAN="$ELK_DIR/elk_cleanup.sh"

VERSION_QEMU_IMAGE="${EVSSIM_GUEST_TESTS_GUEST_VM_IMAGE#ubuntu:}"
VERSION_COMPILE_KERNEL="${EVSSIM_KERNEL_COMPILE_CONTAINER#ubuntu:}"
VERSION_COMPILE_QEMU="${EVSSIM_QEMU_COMPILE_CONTAINER#ubuntu:}"
VERSION_COMPILE_HTESTS="${EVSSIM_HOST_TESTS_COMPILE_CONTAINER#ubuntu:}"
VERSION_COMPILE_GTESTS="${EVSSIM_GUEST_TESTS_COMPILE_CONTAINER#ubuntu:}"
VERSION_RUN_SANITY="$VERSION_COMPILE_QEMU"
VERSION_HOST_TESTS="${EVSSIM_HOST_TESTS_RUN_CONTAINER#ubuntu:}"
VERSION_GUEST_TESTS="$VERSION_COMPILE_QEMU"
VERSION_EXOFS_TEST="$VERSION_COMPILE_QEMU"

# sanity checks
[[ -x "$ELK_INSTALL" ]] || { echo "Missing: $ELK_INSTALL"; exit 1; }
[[ -x "$ELK_CLEAN"   ]] || { echo "Missing: $ELK_CLEAN";   exit 1; }

mkdir -p "$LOGS_DIR"

# static trap string (evaluated NOW, not later)
trap "$ELK_CLEAN --complete-cleanup || true" EXIT

# Run tox
env -u http_proxy -u https_proxy -u HTTP_PROXY -u HTTPS_PROXY tox

# Switch nvme-cli to 3.0-a.5 branch
git -C "$EVSSIM_ROOT_PATH/$EVSSIM_NVME_CLI_FOLDER" checkout "$EVSSIM_NVME_CLI_BRANCH" || \
	git -C "$EVSSIM_ROOT_PATH/$EVSSIM_NVME_CLI_FOLDER" checkout --track "origin/$EVSSIM_NVME_CLI_BRANCH"

# build + sanity
./build-docker-image.sh
./build-qemu-image.sh $VERSION_QEMU_IMAGE $VERSION_QEMU_IMAGE
./compile-kernel.sh $VERSION_COMPILE_KERNEL
./compile-qemu.sh $VERSION_COMPILE_QEMU
./compile-host-tests.sh $VERSION_COMPILE_HTESTS
./compile-guest-tests.sh $VERSION_COMPILE_GTESTS
./docker-run-sanity.sh $VERSION_RUN_SANITY

# start ELK (absolute paths)
"$ELK_INSTALL" "$LOGS_DIR" "$ELK_DIR"

# Running Docker Tests
./docker-test-host.sh $VERSION_HOST_TESTS
./docker-test-guest.sh $VERSION_GUEST_TESTS
./docker-test-exofs.sh $VERSION_EXOFS_TEST
