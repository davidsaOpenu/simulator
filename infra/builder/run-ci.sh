#!/bin/bash
set -e

source ./env.sh
source ./elk.sh

# Make sure every run we get a new clear runtime folder
export EVSSIM_RUNTIME_ALWAYS_RESET=yes

: <<'END_COMMENT'
# We source this script since it defines some env-vars that we need, such as ELK ports and container names
./build-elk-test-image.sh
source ./elk-run-stack.sh
./elk-run-tests.sh
END_COMMENT

# Build images
./build-docker-image.sh
./build-qemu-image.sh

# Compile all modules
./compile-kernel.sh
./compile-qemu.sh
./compile-host-tests.sh
./compile-guest-tests.sh

# Run sanity and tests
./docker-run-sanity.sh

./docker-test-host.sh
./docker-test-guest.sh
# TODO reenable after the fix 
# ./docker-test-exofs.sh

# ELK stack tests
trap ./elk-stop-stack.sh EXIT


