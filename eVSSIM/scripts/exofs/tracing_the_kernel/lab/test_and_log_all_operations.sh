#!/bin/bash
sudo umount "${MOUNT_POINT}"

function run_test() {
    ./run.sh $1 $2
    if [ $? -ne 0 ]; then
        echo "Test $1 $2 failed"
        dmesg
        exit 1
    fi
}

dmesg -c > /dev/null

echo \n\> Tracing 'mount' operation...
run_test mount disabled

echo \n\> Tracing 'open' operation...
run_test open disabled

echo \n\> Tracing 'close' operation...
run_test close disabled

echo -e "\n> Tracing 'ls' operation..."
./run.sh ls disabled

echo \n\> Tracing 'write' operation...
run_test write disabled

echo \n\> Tracing 'read' operation...
run_test read disabled

echo \n\> Testing 'rm' operation...
touch /mnt/exofs0/test1
ls /mnt/exofs0
rm /mnt/exofs0/test1
ls /mnt/exofs0

echo \n\> Testing 'mv' operation...
touch /mnt/exofs0/test_move
ls /mnt/exofs0
mv /mnt/exofs0/test_move /mnt/exofs0/moved_correctly 
ls /mnt/exofs0
rm /mnt/exofs0/moved_correctly

# Run benchmarks on exofs
chmod +x exofs_benchmark.sh
./exofs_benchmark.sh

echo All log files can be found at $(pwd)/output
