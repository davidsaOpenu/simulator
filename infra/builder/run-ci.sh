#!/bin/bash
set -e

# always run from the builder dir so env.sh works
cd "$(dirname "${BASH_SOURCE[0]}")"

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

# build + sanity
./build-docker-image.sh
./build-qemu-image.sh
./compile-kernel.sh
./compile-qemu.sh
./compile-host-tests.sh
./compile-guest-tests.sh
./docker-run-sanity.sh

# start ELK (absolute paths)
"$ELK_INSTALL" "$LOGS_DIR" "$ELK_DIR"

# tests
./docker-test-host.sh
./docker-test-guest.sh
./docker-test-exofs.sh
