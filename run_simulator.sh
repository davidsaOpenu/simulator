#!/usr/bin/env bash
#Add simulator comp here 

if [ -z "$1" ]; then
	echo "usage: sudo $0 <IMAGE>"
	exit 1
fi
cd eVSSIM/QEMU/hw
[ -d data ] || mkdir data
[ -z "$(mount -l | grep 'hw/data')" ] && mount -t tmpfs -o size=16g tmpfs ./data
cd data
[ -e ssd.conf ] || ln -s ../../../CONFIG/ssd.conf
cd ..
../x86_64-softmmu/qemu-system-x86_64 -m 2048 -smp 4 -hda "$1" -device nvme -enable-kvm -redir tcp:2222::22
