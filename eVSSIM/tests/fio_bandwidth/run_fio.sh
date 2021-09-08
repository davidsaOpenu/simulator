#!/bin/bash

scenario=$1
TEST_DIR="/home/esd/guest/fio_bandwidth/output"

rmmod dnvme
modprobe nvme
apt-get -y install fio
cd $TEST_DIR

echo "[+] Running fio job '$1' in order to create random bandwidth over the device"

sudo fio $1 > /dev/null
rc=$?
if [ $rc -ne 0 ]; then
    echo "[X] fio failed"
    exit $rc
fi

echo "[+] Finished running fio."
