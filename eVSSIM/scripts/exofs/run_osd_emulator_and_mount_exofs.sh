#!/bin/bash
set -e

OUTPUT_DIR=${OUTPUT_DIR:-/tmp/output}

# Check if the script is being run as root
if [[ $EUID -ne 0 ]]; then
  echo "Please execute 'sudo su' before executing this script."
  exit 1
fi

NVME_DEV=/dev/nvme0n1
MOUNT_POINT=/mnt/exofs0
TEST_UTIL_DIR=/home/esd/exofs/tracing_the_kernel/lab
PID=0x10000

cd guest
./nvme set-feature $NVME_DEV -f 0xc0 --value=1

# Install dependencies then creating client
echo "> Installing dependencies..."
sudo env DEBIAN_FRONTEND=noninteractive apt-get -o Dpkg::Use-Pty=0 install -y open-iscsi open-iscsi-utils make gcc strace fio bc
pushd /home/esd/exofs/tracing_the_kernel/lab && make && popd

echo "> Setup OSD emulation..."
cd /home/esd/osc-osd/
echo -n "
NUM_TARGETS=1
LOG_FILE=./otgtd.log
OSDNAME[1]=my_osd
BACKSTORE[1]=/var/otgt/otgt-1
" > ./up.conf;
sudo ./up
cd -

sudo iscsiadm -m discovery -t st -p 127.0.0.1
sudo iscsiadm -m node -T esd-.var.otgt.otgt-1 -p 127.0.0.1 --login

echo "> Creating exofs image at $NVME_DEV..."
sudo mkfs.exofs --pid=$PID --dev $NVME_DEV

echo "> Mounting exofs on $MOUNT_POINT..."
sudo mkdir -p $MOUNT_POINT
cd $TEST_UTIL_DIR
sudo -E $TEST_UTIL_DIR/test_and_log_all_operations.sh

echo "> Done!"

# Diagnostics: show the actual mount state at check time so a magic-number
# mismatch tells us whether exofs was never mounted, was unmounted, or was
# replaced by another fs.
echo "--- mount state at magic-number check ---"
echo "[/proc/mounts entries for $MOUNT_POINT or nvme0n1]:"
grep -E "$MOUNT_POINT|nvme0n1" /proc/mounts || echo "  (none)"
echo "[mount -t exofs]:"
mount -t exofs || echo "  (none)"
echo "[ls -la $MOUNT_POINT]:"
ls -la "$MOUNT_POINT" 2>&1 | head -10 || true
echo "--- end mount state ---"

magic_number=$(stat -fc '%t' $MOUNT_POINT)

if [[ $magic_number == "5df5" ]]; then
    echo "The magic number is valid."
else
    echo "The magic number is invalid. It is 0x$magic_number instead of 0x5df5."
    echo "--- last 80 lines of guest dmesg ---"
    dmesg | tail -80 || true
    echo "--- end dmesg ---"
    exit 1
fi
