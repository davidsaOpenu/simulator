#!/bin/bash
set -e

osd_dev=/dev/osd0
mount_point=~/files
pid=0x10000

echo "> Installing dependencies..."
sudo apt install -y open-iscsi open-iscsi-utils


echo "> Setup OSD emulation..."
cd ~/osc-osd/
echo -n "
NUM_TARGETS=1
LOG_FILE=./otgtd.log
BACKSTORE[1]=/var/otgt/otgt-1
" > ./up.conf;
./up
cd -

sudo iscsiadm -m discovery -t st -p 127.0.0.1
sudo iscsiadm -m node -T esd-.var.otgt.otgt-1 -p 127.0.0.1 --login

echo "> Creating exofs image at $osd_dev..."
sudo mkfs.exofs --pid=$pid --dev $osd_dev

echo "> Mounting exofs on $mount_point..."
mkdir -p $mount_point
sudo mount -t exofs -o pid=$pid $osd_dev $mount_point

echo "> Done!"
