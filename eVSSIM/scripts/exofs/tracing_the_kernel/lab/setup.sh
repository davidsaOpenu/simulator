#!/bin/bash
set -e

sudo mount -t debugfs none /sys/kernel/debug

osd_dev=/dev/osd0
mount_point=/mnt/osd0
pid=0x10000

echo "> Installing dependencies..."
sudo apt install -y open-iscsi open-iscsi-utils


echo "> Setup OSD emulation..."
cd ~/osc-osd/
echo -n "
NUM_TARGETS=1
LOG_FILE=./otgtd.log
OSDNAME[1]=my_osd
BACKSTORE[1]=/var/otgt/otgt-1
" > ./up.conf;
./up
cd -

sudo iscsiadm -m discovery -t st -p 127.0.0.1
sudo iscsiadm -m node -T esd-.var.otgt.otgt-1 -p 127.0.0.1 --login

echo "> Creating exofs image at $osd_dev..."
sudo mkfs.exofs --pid=$pid --dev $osd_dev

echo "> mkdir $mount_point..."
sudo mkdir -p $mount_point

echo "> Done!"
