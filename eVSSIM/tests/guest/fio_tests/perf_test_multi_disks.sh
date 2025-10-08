#!/bin/bash

test_name="test_multi_disk"
curr_fio_test_filename="./fio_jobs/"$test_name".fio"

# Zero the devices
# Find test size and device
test_size=`grep size= $curr_fio_test_filename`
test_size=${test_size//[!0-9]/} #remove non digits

device=/dev/nvme1n1 #todo parse from file
echo "zeroing "$test_size"m on $device "
echo time dd if=/dev/zero of=$device bs=1M count=$test_size

device=/dev/nvme2n1 #todo parse from file
echo "zeroing "$test_size"m on $device "
echo time dd if=/dev/zero of=$device bs=1M count=$test_size

# Run test
fio $curr_fio_test_filename
exit $?
