#!/bin/bash
set -Eeo pipefail
trap 'ec=$?; echo "[run-ci.sh] FAILED with exit $ec on: $BASH_COMMAND" >&2' ERR

# always run from the builder dir so env.sh works
cd "$(dirname "${BASH_SOURCE[0]}")"

# --long-run (default): sweep host simulation tests up to a 2 TB simulated disk.
# --fast-run: cap host simulation tests at a 128 MB simulated disk, for quick local iteration.
# EVSSIM_HOST_TEST_MAX_DISK_MB is exported by load_config.sh (sourced via env.sh below) based on RUN_MODE.
RUN_MODE="fast-run"
for arg in "$@"; do
    case "$arg" in
        --long-run|--long_run)
            RUN_MODE="long-run"
            ;;
        --fast-run|--fast_run)
            RUN_MODE="fast-run"
            ;;
        *)
            echo "Unknown option: $arg (expected --long-run or --fast-run)" >&2
            exit 1
            ;;
    esac
done

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
./build-qemu-image.sh                 # 10:18 - 10:30 -- 12 min
./compile-kernel.sh                   # 10:30 - 10:34 -- 4  min
./compile-qemu.sh                     # 10:34 - 10:36 -- 2  min
./compile-host-tests.sh               # 10:36 - 10:37 -- 1  min
./compile-guest-tests.sh              # 10:37 - 10:41 -- 4  min
./docker-run-sanity.sh                # 10:41 - 10:42 -- 1  min

# start ELK (absolute paths)
"$ELK_INSTALL" "$LOGS_DIR" "$ELK_DIR" # 10:42 - 10:48 -- 6  min

# Running Docker Tests
./docker-test-host.sh                 # 10:48 - 13:33 -- 2 h 45 min
./docker-test-guest.sh                # 13:33 - 16:01 -- 2 h 33 min
./docker-test-exofs.sh                # 16:01 - 16:04 --     3  min

# podman step (runs from the job)       16:04 - 16:19 --     15 min
