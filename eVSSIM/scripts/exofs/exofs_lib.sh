#!/bin/bash
# Source and call setup_exofs / teardown_exofs with NVME_DEV and MOUNT_POINT
# set. NVME_DEV must be /dev/nvme0n1: fs/exofs/super.c sends its key-value
# commands to that literal path. The mount itself goes through a local osc-osd
# iSCSI target (/dev/osd0).

EXOFS_PID=0x10000
EXOFS_OSD_DEV=/dev/osd0
EXOFS_OSD_HOME=/home/esd/osc-osd
EXOFS_PORTAL=127.0.0.1
EXOFS_BACKSTORE=/var/otgt/otgt-1
# Filled in by setup_exofs from the discovery output, so it never depends on
# how osc-osd's ./up names the target
EXOFS_TARGET=

setup_exofs() {
    nvme set-feature "${NVME_DEV:?}" -f 0xc0 --value=1 || return 1

    printf 'NUM_TARGETS=1\nLOG_FILE=./otgtd.log\nOSDNAME[1]=my_osd\nBACKSTORE[1]=%s\n' "$EXOFS_BACKSTORE" \
        > "$EXOFS_OSD_HOME/up.conf" || return 1
    ( cd "$EXOFS_OSD_HOME" && ./up ) || return 1

    # Each line reads "<portal>:<port>,<tpgt> <target name>"; the name ends with
    # the backstore path, with its slashes turned into dots
    local discovery
    discovery=$(iscsiadm -m discovery -t st -p "$EXOFS_PORTAL") || return 1
    EXOFS_TARGET=$(awk -v suffix="${EXOFS_BACKSTORE//\//.}" \
        'substr($2, length($2) - length(suffix) + 1) == suffix { print $2; exit }' <<< "$discovery")
    if [ -z "$EXOFS_TARGET" ]; then
        echo "ERROR no iSCSI target for $EXOFS_BACKSTORE at $EXOFS_PORTAL; discovery returned:" >&2
        echo "$discovery" >&2
        return 1
    fi
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
    # Without a discovered target, log out of every session on the portal
    if [ -n "$EXOFS_TARGET" ]; then
        iscsiadm -m node -T "$EXOFS_TARGET" -p "$EXOFS_PORTAL" --logout >/dev/null 2>&1 || true
    else
        iscsiadm -m node -p "$EXOFS_PORTAL" --logout >/dev/null 2>&1 || true
    fi
    ( cd "$EXOFS_OSD_HOME" && ./up down ) >/dev/null 2>&1 || true
    rmdir "$MOUNT_POINT" 2>/dev/null || true
    return $rc
}
