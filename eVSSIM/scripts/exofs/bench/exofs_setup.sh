#!/bin/bash
#
# Bring up OSD emulation and mount exofs -- the setup half of
# run_osd_emulator_and_mount_exofs.sh, without the ftrace lab suite.
#
# Kept separate so a benchmark or a correctness test can get a mounted exofs
# without paying for six function_graph traces first. T1's setup_exofs should
# grow out of this.
#
set -e

NVME_DEV=${NVME_DEV:-/dev/nvme0n1}
OSD_DEV=${OSD_DEV:-/dev/osd0}
MOUNT_POINT=${MOUNT_POINT:-/mnt/exofs0}
PID=${PID:-0x10000}

if [[ $EUID -ne 0 ]]; then
    echo "ERROR must run as root" >&2
    exit 1
fi

echo "> Enabling the key-value command set on $NVME_DEV..."
cd /home/esd/guest
./nvme set-feature "$NVME_DEV" -f 0xc0 --value=1

# Trusty is EOL, so its archives may be unreachable. Only reach for the network
# if something is actually missing, and do not let that failure mask a working
# image.
missing=""
command -v iscsiadm >/dev/null 2>&1 || missing="$missing open-iscsi"
if [[ -n "$missing" ]]; then
    echo "> Installing:$missing"
    env DEBIAN_FRONTEND=noninteractive apt-get -o Dpkg::Use-Pty=0 install -y $missing \
        || echo "WARNING apt-get failed; continuing with what the image already has"
fi

echo "> Starting the OSD target..."
cd /home/esd/osc-osd/
printf 'NUM_TARGETS=1\nLOG_FILE=./otgtd.log\nOSDNAME[1]=my_osd\nBACKSTORE[1]=/var/otgt/otgt-1\n' > ./up.conf
./up

echo "> Attaching over iSCSI..."
iscsiadm -m discovery -t st -p 127.0.0.1
iscsiadm -m node -T esd-.var.otgt.otgt-1 -p 127.0.0.1 --login

# The OSD ULD creates the char device asynchronously after login.
for _ in $(seq 1 30); do
    [[ -e "$OSD_DEV" ]] && break
    sleep 1
done
if [[ ! -e "$OSD_DEV" ]]; then
    echo "ERROR $OSD_DEV never appeared after iSCSI login" >&2
    exit 1
fi

echo "> Creating the exofs image on $NVME_DEV..."
mkfs.exofs --pid="$PID" --dev "$NVME_DEV"

echo "> Mounting exofs on $MOUNT_POINT..."
mkdir -p "$MOUNT_POINT"
dmesg -c > /dev/null 2>&1 || true
if ! mount -t exofs -o "pid=$PID" "$OSD_DEV" "$MOUNT_POINT"; then
    echo "ERROR mount failed; kernel said:" >&2
    dmesg | tail -50 >&2
    exit 1
fi

magic=$(stat -fc '%t' "$MOUNT_POINT")
if [[ "$magic" != "5df5" ]]; then
    echo "ERROR bad magic: 0x$magic (expected 0x5df5)" >&2
    dmesg | tail -40
    exit 1
fi
echo "> exofs mounted, magic 0x$magic"
