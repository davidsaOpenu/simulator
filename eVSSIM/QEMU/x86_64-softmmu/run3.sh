#!/bin/bash

run_path=`dirname $0`
[[ "x$run_path" != "x." ]] && cd $run_path && echo "moved to "$(pwd)

all_dat_files=`ls ./data/*.dat`
need_dat_rebuild=0

for curr_dat_file in $all_dat_files; do
	if [ $curr_dat_file -ot "./data/ssd.conf" ];then
		echo $curr_dat_file" needs to be rebuilt"
		need_dat_rebuild=1
	fi
done

if [ $need_dat_rebuild -ne 0 ]; then
	echo 'removing dat files'
	rm $all_dat_files
fi

#ssd ram image
../../RAMDISK/ram_mount.sh

os_img=$QEMU_OS_IMAGE_PATH
# read base OS image path
if [ -f os_img_path.conf ]; then
	os_img=`cat os_img_path.conf`
fi
echo "running using os image $os_img:"
echo "./qemu-system-x86_64 -m 2048 -smp 2 -redir tcp:2222::22 -hdb ../../RAMDISK/rd/ssd.img -hda $os_img"

#./qemu-system-x86_64 -m 2048 -smp 2 -redir tcp:2222::22 -hdb ../../RAMDISK/rd/ssd.img -hda $os_img


