#!/bin/bash
# Source and call setup_exofs / teardown_exofs with NVME_DEV and MOUNT_POINT
# set. NVME_DEV must be /dev/nvme0n1: fs/exofs/super.c sends its key-value
# commands to that literal path. The mount itself goes through a local osc-osd
# iSCSI target (/dev/osd0).

EXOFS_PID=0x10000
EXOFS_OSD_DEV=/dev/osd0
EXOFS_OSD_HOME=/home/esd/osc-osd
EXOFS_PORTAL=127.0.0.1
# The target name osc-osd's ./up builds from the hostname and backstore path
EXOFS_TARGET="$(hostname)-.var.otgt.otgt-1"

setup_exofs() {
    /home/esd/guest/nvme set-feature "${NVME_DEV:?}" -f 0xc0 --value=1 || return 1

    printf 'NUM_TARGETS=1\nLOG_FILE=./otgtd.log\nOSDNAME[1]=my_osd\nBACKSTORE[1]=/var/otgt/otgt-1\n' \
        > "$EXOFS_OSD_HOME/up.conf" || return 1
    ( cd "$EXOFS_OSD_HOME" && ./up ) || return 1

    iscsiadm -m discovery -t st -p "$EXOFS_PORTAL" || return 1
    iscsiadm -m node -T "$EXOFS_TARGET" -p "$EXOFS_PORTAL" --login || return 1

    # --login returns before udev creates the node, and mounting without it
    # oopses this kernel instead of failing
    local waited=0
    while [ ! -e "$EXOFS_OSD_DEV" ]; do
        [ $waited -lt 30 ] || { echo "ERROR $EXOFS_OSD_DEV did not appear" >&2; return 1; }
        sleep 1
        waited=$((waited + 1))
    done

    mkfs.exofs --pid="$EXOFS_PID" --dev "$NVME_DEV" || return 1
    mkdir -p "${MOUNT_POINT:?}" || return 1
    mount -t exofs -o "pid=$EXOFS_PID" "$EXOFS_OSD_DEV" "$MOUNT_POINT" || return 1
}

# Every step runs even if an earlier one fails, so a partial setup is cleaned
# up too; only a failed umount is reported.
teardown_exofs() {
    local rc=0
    if grep -qs " ${MOUNT_POINT:?} " /proc/mounts; then
        umount "$MOUNT_POINT" || rc=1
    fi
    iscsiadm -m node -T "$EXOFS_TARGET" -p "$EXOFS_PORTAL" --logout >/dev/null 2>&1 || true
    ( cd "$EXOFS_OSD_HOME" && ./up down ) >/dev/null 2>&1 || true
    rmdir "$MOUNT_POINT" 2>/dev/null || true
    return $rc
}
