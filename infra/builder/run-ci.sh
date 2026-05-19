#!/bin/bash
set -Eeo pipefail
trap 'ec=$?; echo "[run-ci.sh] FAILED with exit $ec on: $BASH_COMMAND" >&2' ERR

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

# sanity checks
[[ -x "$ELK_INSTALL" ]] || { echo "Missing: $ELK_INSTALL"; exit 1; }
[[ -x "$ELK_CLEAN"   ]] || { echo "Missing: $ELK_CLEAN";   exit 1; }

mkdir -p "$LOGS_DIR"

# static trap string (evaluated NOW, not later)
trap "$ELK_CLEAN --complete-cleanup || true" EXIT

# Run tox
env -u http_proxy -u https_proxy -u HTTP_PROXY -u HTTPS_PROXY tox

# build + sanity
./build-docker-image.sh
./build-qemu-image.sh $EVSSIM_GUEST_TESTS_GUEST_VM_IMAGE $EVSSIM_GUEST_TESTS_GUEST_VM_BUILD_CONTAINER
./compile-kernel.sh $EVSSIM_KERNEL_COMPILE_CONTAINER
./compile-qemu.sh ubuntu-26.04
./compile-qemu.sh ubuntu-14.04 # make sure this is second to simplify docker-run-sanity.sh on the correct qemu branch (as it is expecting 14.04 structure atm)
./compile-host-tests.sh $EVSSIM_HOST_TESTS_COMPILE_CONTAINER
./compile-guest-tests.sh $EVSSIM_GUEST_TESTS_COMPILE_CONTAINER
./docker-run-sanity.sh $EVSSIM_QEMU_COMPILE_CONTAINER

# start ELK (absolute paths)
"$ELK_INSTALL" "$LOGS_DIR" "$ELK_DIR"

# Running Docker Tests
./docker-test-host.sh $EVSSIM_HOST_TESTS_RUN_CONTAINER
./docker-test-guest.sh $EVSSIM_QEMU_COMPILE_CONTAINER
./docker-test-exofs.sh $EVSSIM_QEMU_COMPILE_CONTAINER
