#!/bin/bash
set -e

# Build images
./build-docker-image.sh
./build-qemu-image.sh

# Compile all modules
./compile-kernel.sh
./compile-qemu.sh
./compile-monitor.sh
./compile-nvme-tools.sh
./compile-tests.sh

# Run sanity and tests
./docker-run-sanity.sh
./docker-test-host.sh
./docker-test-guest.sh