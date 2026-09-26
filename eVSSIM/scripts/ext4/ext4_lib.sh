#!/bin/bash
# Source and call setup_ext4 / teardown_ext4 with NVME_DEV and MOUNT_POINT set.

setup_ext4() {
    mkdir -p "${MOUNT_POINT:?}" || return 1
    mkfs.ext4 -F "${NVME_DEV:?}" || return 1
    mount -t ext4 "$NVME_DEV" "$MOUNT_POINT" || return 1
}

teardown_ext4() {
    if grep -qs " ${MOUNT_POINT:?} " /proc/mounts; then
        umount "$MOUNT_POINT" || return 1
    fi
    rmdir "$MOUNT_POINT" 2>/dev/null || true
}
