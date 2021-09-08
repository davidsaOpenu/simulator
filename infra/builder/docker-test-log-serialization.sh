#!/bin/bash
source ./builder.sh

# Make a fresh copy
evssim_qemu_fresh_image

# Run qemu with test specific configuration
evssim_qemu_detached

# Run tests inside the guest
set +e
start_time=$(date +%Y-%m-%dT%H:%M:%S.000Z)
evssim_guest "cd ./guest/fio_bandwidth; ls; sleep 4"
end_time=$(date +%Y-%m-%dT%H:%M:%S.000Z)
test_rc=$?
set -e

# Stop qemu and wait
evssim_qemu_flush_disk
evssim_qemu_stop

# Fail if tests fails
if [ $test_rc -ne 0 ]; then
    echo "ERROR Failed creating bandwidth for ELK logs, error=$test_rc"
    exit $test_rc
fi

echo "[+} Succeeded in running the bandwidth script"

# Find the genereated log file
log_file=$(find logs -name 'log_file*')

# Write all the output to the results file later to be parsed by the wrapper
echo "start_time=${start_time}" > /tmp/elk_bandwidth_res.txt
echo "end_time=${end_time}" >> /tmp/elk_bandwidth_res.txt
echo "log_file=${log_file}" >> /tmp/elk_bandwidth_res.txt

exit 0
