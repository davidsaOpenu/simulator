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

# Wait for the osduld character device to appear before running mkfs/mount.
# The in-kernel osduld_info_lookup() table is populated asynchronously after
# the SCSI/OSD attach; mounting exofs before it is populated triggers a
# NULL-pointer deref in exofs_read_lookup_dev_table -> osduld_device_same
# on slower guests (see the block that follows for the failure mode).
echo "> Waiting for /dev/osd0 to appear (osduld device)..."
osduld_ready=0
for i in $(seq 1 30); do
    if [ -c /dev/osd0 ]; then
        echo "  /dev/osd0 present after ${i}s"
        osduld_ready=1
        break
    fi
    sleep 1
done
if [ "$osduld_ready" != "1" ]; then
    echo "WARNING /dev/osd0 did not appear within 30s; proceeding anyway"
fi

echo "> Creating exofs image at $NVME_DEV..."
sudo mkfs.exofs --pid=$PID --dev $NVME_DEV

# Extra settle time for the in-kernel osduld lookup table used by
# exofs_mount. mkfs.exofs uses the userspace osd path, which does not
# guarantee the kernel-side table is populated for exofs's own lookup.
sleep 3

echo "> Mounting exofs on $MOUNT_POINT..."
sudo mkdir -p $MOUNT_POINT
# Mount exofs here (no function_graph tracing) instead of inside the tracing
# harness. The Linux 5.0.0+ in-tree exofs driver hits a NULL-pointer
# dereference in exofs_read_lookup_dev_table -> osduld_device_same when
# mount(2) runs under the ftrace function_graph tracer on the CI guest;
# see test_and_log_all_operations.sh for context.
set +e
sudo mount -t exofs -o pid=$PID $NVME_DEV $MOUNT_POINT
mount_rc=$?
set -e
if [ $mount_rc -ne 0 ]; then
    echo "ERROR userspace mount of exofs failed (rc=$mount_rc)"
    echo "--- guest dmesg (last 80 lines) ---"
    dmesg | tail -80 || true
    echo "--- /proc/mounts (head -40) ---"
    head -40 /proc/mounts || true
    echo "--- lsmod | head -20 ---"
    lsmod 2>&1 | head -20 || true
    echo "--- end mount-failure diagnostics ---"
    exit $mount_rc
fi
echo "Mounted $NVME_DEV on $MOUNT_POINT with filesystem type exofs and data pid=$PID"
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
