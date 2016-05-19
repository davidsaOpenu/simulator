#!/bin/bash

if [ $# -lt 1 ]; then
	echo 'usage: perf_test test_name'
	exit -1
fi

test_name=$1
reference_res_filename="./reference_results/"$test_name"_ref_res"
curr_fio_test_filename="./fio_jobs/"$test_name".fio"
curr_res_filename="/tmp/"$test_name"_res"

extract_iops(){
	local fn=$1
	local grep_param=$2
	local __resultvar=$3
	local iops=-1
	if [ 1 -eq `grep $grep_param $fn | grep 'runt' -c` ]; then
		iops_str=`grep $grep_param $fn | grep 'runt' | awk '{print $4}'`
		iops=${iops_str//[!0-9]/} #remove non digits
	fi
	eval $__resultvar=$iops
}

for test_type in 'write' 'read' ; do 
extract_iops $reference_res_filename $test_type reference_result
echo "iops reference res = $reference_result"
if [ $reference_result -eq -1 ]; then
	# test_type not applicable
	echo "skipping $test_type test"
	continue
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

#run test
fio $curr_fio_test_filename > $curr_res_filename

extract_iops $curr_res_filename $test_type curr_result
echo "extract_iops curr res = $curr_result"

if [ $reference_result -lt 1 ]; then
	echo "ERROR failed to find reference result"
	exit -1
fi

if [ $curr_result -lt 1 ]; then
	echo "ERROR failed to find current result"
	exit -1
fi

if [ $curr_result -gt $reference_result ];
then
	iops_diff=$((curr_result - reference_result))	
	iops_diff_percent=$((iops_diff*100/reference_result))
	echo "curr result is worse! - reference result = $reference_result, curr result = $curr_result, iops diff = $iops_diff( $iops_diff_percent %)"
	if [ $iops_diff_percent -gt 4 ]; then
		echo "$test_name ERROR $test_type perf hit of $iops_diff_percent%"
	else
		echo "$test_name WARN $test_type perf hit of $iops_diff_percent%"
	fi
else
	iops_diff=$((reference_result - curr_result))	
	iops_diff_percent=$((iops_diff*100/reference_result))
	echo "curr result is better! - reference result = $reference_result, curr result = $curr_result, iops diff = $iops_diff( $iops_diff_percent %)"
	echo "$test_type perf boost of $iops_diff_percent%"
fi

done
