#!/bin/bash

ELK_TEST_DIR="/home/esd/guest/elk_fio"

rmmod dnvme
modprobe nvme
apt-get -y install fio
cd $ELK_TEST_DIR

echo "[+] Running fio in order to create random bandwidth over the device"

fio rw_bandwidth.fio > /dev/null
rc=$?
if [ $rc -ne 0 ]; then
    echo "[X] fio failed"
    exit $rc
fi

echo "[+] Finished running fio."
