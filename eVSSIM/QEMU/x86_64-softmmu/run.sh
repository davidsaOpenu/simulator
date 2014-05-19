#!/bin/bash


mkdir ../../RAMDISK/rd
chmod 0755 ../../RAMDISK/rd
sudo mount -t tmpfs -o size=8g tmpfs ../../RAMDISK/rd


rm -rf data/*.dat
qemu-img create -f qcow  /home/esd/Downloads/VSSIM_v1.1/RAMDISK/rd/ssd_test.img 16G

# check ./CONFIG/ssd.conf make sure it contains the correct path to ssd_test.img


sudo ./qemu-system-x86_64 -m 1024 -hda /media/UBUNTU/QUEMU/ssd.img  -hdb  /home/esd/Downloads/VSSIM_v1.1//RAMDISK/rd/ssd_test.img -smp 2 -redir tcp:2222::22

#-net nic,model=e1000 -net tap,ifname=tap_kvm_1,script=`pwd`/run1.sh

