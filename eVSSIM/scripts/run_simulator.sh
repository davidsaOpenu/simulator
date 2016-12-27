#!/usr/bin/env bash
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
# ../x86_64-softmmu/qemu-system-x86_64 -m 2048 -smp 4 -hda "$1" -device nvme -enable-kvm -redir tcp:2222::22
../x86_64-softmmu/qemu-system-x86_64 -m 2048 -smp 4 -nographic -pidfile /home/chananel/Desktop/git/simulator/pid -hda "$1" -device nvme -enable-kvm -redir tcp:2222::22

# ../x86_64-softmmu/qemu-system-x86_64 -m 2048 -smp 4 -hda "$1" -redir tcp:2222::22 -kernel "$2" -initrd "$3" -append 'BOOT_IMAGE=/boot/vmlinuz-3.16.2 root=UUID=063018ec-674c-4c3e-a976-ac4fa950864f ro' -enable-kvm
# ../x86_64-softmmu/qemu-system-x86_64 -m 2048 -smp 4 -hda "$1" -device nvme -redir tcp:2222::22 -kernel "$2" -initrd "$3" -append 'BOOT_IMAGE=/boot/vmlinuz-3.16.2 root=UUID=063018ec-674c-4c3e-a976-ac4fa950864f ro' -enable-kvm -s -S
