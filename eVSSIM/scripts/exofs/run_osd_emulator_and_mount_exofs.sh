#!/bin/bash
set -e

OUTPUT_DIR=${OUTPUT_DIR:-/tmp/output}

# Check if the script is run as root
if [[ $EUID -ne 0 ]]; then
  echo "Please execute 'sudo su' before executing this script."
  exit 1
fi

nvme_dev=/dev/nvme0n1
mount_point=/mnt/exofs0
test_util_dir=/home/esd/exofs/tracing_the_kernel/lab
test_cli="$test_util_dir/test"
pid=0x10000

cd guest
./nvme set-feature /dev/nvme0n1 -f 0xc0 --value=1

# todo install dependencies then creating client
echo "> Installing dependencies..."
sudo apt install -y open-iscsi open-iscsi-utils make gcc
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

echo "> Creating exofs image at $nvme_dev..."
sudo mkfs.exofs --pid=$pid --dev $nvme_dev

echo "> Mounting exofs on $mount_point..."
sudo mkdir -p $mount_point
cd /home/esd/exofs/tracing_the_kernel/lab
sudo OUTPUT_DIR="$OUTPUT_DIR" $test_util_dir/test_and_log_all_operations.sh

echo "> Done!"

magic_number=$(stat -fc '%t' $mount_point)

if [[ $magic_number == "5df5" ]]; then
    echo "The magic number is valid."
else
    echo "The magic number is invalid. It is 0x$magic_number instead of 0x5df5."
    exit 1
fi
