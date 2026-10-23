#!/bin/bash
set -Eeo pipefail
trap 'ec=$?; echo "[run-ci.sh] FAILED with exit $ec on: $BASH_COMMAND" >&2' ERR

# always run from the builder dir so env.sh works
cd "$(dirname "${BASH_SOURCE[0]}")"

if [[ -n "${EVSSIM_QEMU_INPUT_RECORD:-}" ]]; then
    echo "ERROR: run-ci.sh does not consume saved QEMU input records yet. Use the artifact helper for explicit builds; workflow integration remains S7." >&2
    exit 1
fi

# Preserve the existing mixed legacy CI environment unless the caller
# explicitly selects another legacy profile. Artifact resolution separately
# defaults to profile 2; it must not change this workflow's default.
export EVSSIM_VERSIONS_CONFIGURATION_ID="${EVSSIM_VERSIONS_CONFIGURATION_ID-5}"
source ./env.sh

# The launcher still uses the legacy binary layout. A modern profile must
# not run that binary in a modern container or be silently replaced by legacy.
if [[ "$EVSSIM_QEMU_BRANCH" != master || "$EVSSIM_QEMU_COMPILE_CONTAINER" != ubuntu-14.04 ]]; then
    echo "ERROR: run-ci.sh currently tests legacy QEMU master in ubuntu-14.04; profile $EVSSIM_VERSIONS_CONFIGURATION_ID selects $EVSSIM_QEMU_BRANCH in $EVSSIM_QEMU_COMPILE_CONTAINER. QEMU 11 runtime integration remains S3/S7." >&2
    exit 1
fi

# Check all required tools are installed after rejecting unsupported requests.
./check_tools.sh
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
# Check modern compilation separately, then build the selected legacy runtime.
# Only the modern check changes its child profile; the final build and tests
# use the same parent configuration.
(
    EVSSIM_VERSIONS_CONFIGURATION_ID=$(jq -er '.qemu_branches["11.0"]' "$EVSSIM_ROOT_PATH/$EVSSIM_VERSIONS_FOLDER/qemu-build-policy.json")
    export EVSSIM_VERSIONS_CONFIGURATION_ID
    source ./load_config.sh
    ./compile-qemu.sh "$EVSSIM_QEMU_COMPILE_CONTAINER"
)
./compile-qemu.sh "$EVSSIM_QEMU_COMPILE_CONTAINER"
./compile-host-tests.sh $EVSSIM_HOST_TESTS_COMPILE_CONTAINER
./compile-guest-tests.sh $EVSSIM_GUEST_TESTS_COMPILE_CONTAINER
./docker-run-sanity.sh $EVSSIM_QEMU_COMPILE_CONTAINER

# start ELK (absolute paths)
"$ELK_INSTALL" "$LOGS_DIR" "$ELK_DIR"

# Running Docker Tests
./docker-test-host.sh $EVSSIM_HOST_TESTS_RUN_CONTAINER
./docker-test-host-elk.sh $EVSSIM_HOST_TESTS_RUN_CONTAINER
./docker-test-guest.sh $EVSSIM_QEMU_COMPILE_CONTAINER
./docker-test-exofs.sh $EVSSIM_QEMU_COMPILE_CONTAINER
