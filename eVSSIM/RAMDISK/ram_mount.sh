sudo mkdir rd -p
path=`dirname $0`
path=`readlink -e $path`
if [ 0 -eq `mount | grep "$path/rd" -c` ]; then 
	echo "mounting $path"
	sudo mount -t tmpfs -o size=16g tmpfs ./rd
#else
	#echo "$path already mounted"
fi

rd_path="$path/rd"

sudo chmod 0777 $rd_path

ram_image=$rd_path'/ssd.img'
if [ -f $ram_image ]; then
	#echo "ram image $ram_image already exist"
else
	echo "creating ram image $ram_image"
	../QEMU/qemu-img create -f qcow $ram_image 16G
	echo "DONE"
fi
