#!/bin/bash

if [ $# -lt 1 ]; then
	echo 'usage: perf_test test_name'
	exit -1
fi

test_name=$1
reference_res_base_filename=$test_name"_ref_res"
reference_res_filename="./reference_results/"$reference_res_base_filename
curr_fio_test_filename="./fio_jobs/"$test_name".fio"

if [ ! -f $curr_fio_test_filename ]; then
	echo "file not found: $curr_fio_test_filename"
	exit -1
fi

#zero the device
if [ 1 -eq `grep -v '#filename' $curr_fio_test_filename | grep -c filename=/dev/nvme0n1` ];
then
	# find test size and device
	test_size=`grep size= $curr_fio_test_filename`
	test_size=${test_size//[!0-9]/} #remove non digits

	device=/dev/nvme0n1 #todo parse from file
	echo "zeroing "$test_size"m on $device "
	echo time dd if=/dev/zero of=$device bs=1M count=$test_size
else
	echo "no active filename line in $curr_fio_test_filename"
	exit -1
fi

#store old result, if exist
if [ -f $reference_res_filename ]; then
	mkdir -p ./reference_results/backups || exit 1
	bck_fn="./reference_results/backups/"$reference_res_base_filename"_bck_`date +%G%m%d%H%M%s`"
	echo "test results already exist, backing up as $bck_fn"
	cp "$reference_res_filename" "$bck_fn" || exit 1
fi

#run test
echo "running test $curr_fio_test_filename, recording results in $reference_res_filename"
# Keep each raw invocation, including failures, without publishing a failed
# or incomplete measurement as a replacement for the previous reference.
raw_result=$(mktemp "${reference_res_filename}.XXXXXXXX") || exit 1
echo "raw result: $raw_result"
fio "$curr_fio_test_filename" > "$raw_result" 2>&1
status=$?
if [ "$status" -ne 0 ]; then
    echo "fio failed with status $status; output: $raw_result" >&2
    exit "$status"
fi
python3 ./fio_metrics.py validate "$curr_fio_test_filename" "$raw_result" || exit 1
cp "$raw_result" "$reference_res_filename" || exit 1
# Recording runs as root. Preserve the old reference's readability for the
# existing guest-user evidence copy (mktemp creates mode 0600 by default).
chmod a+r "$reference_res_filename"
