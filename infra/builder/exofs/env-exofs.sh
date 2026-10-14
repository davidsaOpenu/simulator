#!/bin/bash
# Environment for the standalone exofs guest test.
#
# Source this file AFTER env.sh (from the builder folder). It only overrides
# variables for the current process, so the shared infrastructure (dist/,
# kernel 5.0.0 build, configs.json, open-osd flow) is never touched:
#
#   * all artifacts go to a separate dist folder ($EXOFS_DIST_FOLDER, default
#     dist-exofs) next to the regular dist folder
#   * the kernel is built from its own tree ($EXOFS_KERNEL_FOLDER, default a
#     git worktree "kernel-exofs" of the kernel repo at $EXOFS_KERNEL_REF)
#   * qemu runs with its own SSD configuration (object strategy, 2 drives)

if [ -z "${EVSSIM_ENVIRONMENT:-}" ]; then
    echo "ERROR source env.sh before exofs/env-exofs.sh"
    return 1
fi

export EXOFS_BUILDER_SUBFOLDER="$EVSSIM_BUILDER_FOLDER/exofs"

# Where every artifact of this test lives (relative to EVSSIM_ROOT_PATH)
export EXOFS_DIST_FOLDER="${EXOFS_DIST_FOLDER:-dist-exofs}"

# Kernel tree used for the 7.0.2 + exofs build (relative to EVSSIM_ROOT_PATH).
# Default: a dedicated worktree, so the shared "kernel" checkout is untouched.
# For quick local iterations you may point it at an existing 7.0.2 tree:
#   EXOFS_KERNEL_FOLDER=kernel ./exofs/build.sh kernel
export EXOFS_KERNEL_FOLDER="${EXOFS_KERNEL_FOLDER:-kernel-exofs}"
# Git ref of the kernel repo used to create the worktree
export EXOFS_KERNEL_REF="${EXOFS_KERNEL_REF:-7.0.2}"
# Optional GerritHub change ref to cherry-pick into the worktree,
# e.g. refs/changes/98/1245198/3
export EXOFS_KERNEL_GERRIT_REF="${EXOFS_KERNEL_GERRIT_REF:-}"
export EXOFS_KERNEL_GERRIT_URL="${EXOFS_KERNEL_GERRIT_URL:-https://review.gerrithub.io/davidsaOpenu/kernel}"
# exofs change cherry-picked into the worktree only when fs/exofs is missing
# (the server's 7.0.2 branch is plain Linux); set empty to disable.
export EXOFS_KERNEL_CHANGE="${EXOFS_KERNEL_CHANGE-1245198}"
# Base kernel configuration + exofs fragment
export EXOFS_KERNEL_BASE_CONFIG="${EXOFS_KERNEL_BASE_CONFIG:-$EVSSIM_VERSIONS_FOLDER/kernel/7.0.2/.config}"
export EXOFS_KERNEL_CONFIG_FRAGMENT="$EXOFS_BUILDER_SUBFOLDER/kernel.config"
# If the base config is missing (CR 1237773 not merged yet) it is taken from
# this simulator change; set empty to disable.
export EXOFS_KERNEL_CONFIG_CHANGE="${EXOFS_KERNEL_CONFIG_CHANGE-1237773}"
export EXOFS_SIMULATOR_GERRIT_URL="${EXOFS_SIMULATOR_GERRIT_URL:-https://review.gerrithub.io/davidsaOpenu/simulator}"

# Project qemu used by this test: a dedicated worktree of the qemu repo
# ($EXOFS_QEMU_FOLDER) with the NVMe KV offset change (CR $EXOFS_QEMU_CHANGE)
# cherry-picked, built separately - the shared ~/qemu checkout and its build
# are never touched.
export EXOFS_QEMU_FOLDER="${EXOFS_QEMU_FOLDER:-qemu-exofs}"
export EXOFS_QEMU_REF="${EXOFS_QEMU_REF:-master}"
export EXOFS_QEMU_GERRIT_URL="${EXOFS_QEMU_GERRIT_URL:-https://review.gerrithub.io/davidsaOpenu/qemu}"
export EXOFS_QEMU_CHANGE="${EXOFS_QEMU_CHANGE-1245496}"     # set empty to skip

# Containers
# Kernel 7.0.2 needs a modern toolchain
export EXOFS_KERNEL_COMPILE_CONTAINER="${EXOFS_KERNEL_COMPILE_CONTAINER:-ubuntu-26.04}"
# Guest tools (nvme-cli, mkfs.exofs) must link against the 14.04 glibc, the
# guest image is built with the 14.04 tooling, and the project qemu is built
# (and therefore run) in the 14.04 container.
export EXOFS_GUEST_CONTAINER="${EXOFS_GUEST_CONTAINER:-ubuntu-14.04}"

# Optional: reuse an existing *pristine* Ubuntu 14.04 image instead of
# downloading/building one (path relative to EVSSIM_ROOT_PATH or absolute).
export EXOFS_BASE_IMAGE="${EXOFS_BASE_IMAGE:-}"

# Test knobs (forwarded into the guest)
export EXOFS_STRICT="${EXOFS_STRICT:-0}"
export EXOFS_LARGE_FILE_MB="${EXOFS_LARGE_FILE_MB:-8}"
export EXOFS_MANY_FILES="${EXOFS_MANY_FILES:-200}"
export EXOFS_TEST_TIMEOUT="${EXOFS_TEST_TIMEOUT:-3600}"
export EXOFS_TESTS="${EXOFS_TESTS:-}"

# ---- Overrides of the shared builder variables (this process only) -------
export EVSSIM_DIST_FOLDER="$EXOFS_DIST_FOLDER"
export EVSSIM_DATA_FOLDER="${EXOFS_DATA_FOLDER:-data-exofs}"   # simulator runtime data (ssd.conf, *.dat)
export EVSSIM_QEMU_FOLDER="$EXOFS_QEMU_FOLDER"
export EVSSIM_CREATE_FOLDERS="$EVSSIM_DATA_FOLDER $EVSSIM_DIST_FOLDER $EVSSIM_LOGS_FOLDER"
# Only our own qemu worktree gets the data link (created once the worktree exists)
if [ -d "$EVSSIM_ROOT_PATH/$EXOFS_QEMU_FOLDER/hw" ]; then
    export EVSSIM_DATA_LINKED_FOLDER="$EVSSIM_ROOT_PATH/$EXOFS_QEMU_FOLDER/hw/data"
else
    export EVSSIM_DATA_LINKED_FOLDER=""
fi
export EVSSIM_RUNTIME_SSD_CONF_TEMPLATE="$EVSSIM_ROOT_PATH/$EXOFS_BUILDER_SUBFOLDER/ssd.conf.template"
export EVSSIM_RUNTIME_STORAGE_STRATEGY=2      # object strategy (key/value)
export EVSSIM_QEMU_SIMULATOR_ENABLED=yes
export EVSSIM_RUNTIME_ALWAYS_RESET=yes
# Own ssh/VNC ports so the test can run next to the regular VM (which holds
# 2222, 2003 and 5900). Only ssh + VNC are published; 2003 (simulator monitor)
# stays inside the container.
export EVSSIM_QEMU_SSH_PORT="${EXOFS_QEMU_SSH_PORT:-2223}"
export EVSSIM_QEMU_VNC="${EXOFS_QEMU_VNC:-7}"
export EVSSIM_DOCKER_PORTS_OPTION="-p $EVSSIM_QEMU_SSH_PORT:$EVSSIM_QEMU_SSH_PORT -p $((5900 + EVSSIM_QEMU_VNC)):$((5900 + EVSSIM_QEMU_VNC))"

# Kernel release produced by "exofs/build.sh kernel" (e.g. 7.0.2+)
export EXOFS_KERNEL_RELEASE_FILE="$EVSSIM_ROOT_PATH/$EXOFS_DIST_FOLDER/kernel/kernel.release"
if [ -f "$EXOFS_KERNEL_RELEASE_FILE" ]; then
    EVSSIM_KERNEL_DIST="$(cat "$EXOFS_KERNEL_RELEASE_FILE")"
    export EVSSIM_KERNEL_DIST
fi

export EXOFS_IMAGE_TEMPLATE="$EVSSIM_ROOT_PATH/$EXOFS_DIST_FOLDER/$EVSSIM_QEMU_IMAGE.template"
export EXOFS_IMAGE="$EVSSIM_ROOT_PATH/$EXOFS_DIST_FOLDER/$EVSSIM_QEMU_IMAGE"
