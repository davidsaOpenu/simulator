#!/bin/bash
set -e

# Check if the script is run as root
if [[ $EUID -ne 0 ]]; then
  echo "Please execute 'sudo su' before executing this script."
  exit 1
fi

osd_dev=/dev/osd0
nvme_dev=/dev/nvme0n1
mount_point=/home/esd/files
pid=0x10000

cd guest
./nvme set-feature /dev/nvme0n1 -f 0xc0 --value=1

echo "> Installing dependencies..."
sudo apt install -y open-iscsi open-iscsi-utils


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
sudo mount -t exofs -o pid=$pid $osd_dev $mount_point

echo "> Done!"
