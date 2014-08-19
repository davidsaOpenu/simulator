path=`dirname $0`
path=`readlink -e $path`
rd_path="$path/rd"
sudo mkdir $rd_path -p
if [ 0 -eq `mount | grep "$rd_path" -c` ]; then 
	echo "mounting $rd_path"
	sudo mount -t tmpfs -o size=16g tmpfs $rd_path
#else
	#echo "$path already mounted"
fi


sudo chmod 0777 $rd_path

ram_image=$rd_path'/ssd.img'
if [ ! -f $ram_image ]; then
	echo "creating ram image $ram_image"
	$path/../QEMU/qemu-img create -f qcow $ram_image 16G
	echo "DONE"
#else
	#echo "ram image $ram_image already exist"
fi
