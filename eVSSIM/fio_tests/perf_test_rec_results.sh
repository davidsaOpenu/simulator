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

if [ -f $reference_res_filename ]; then
	mkdir -p ./reference_results/backups
	bck_fn="./reference_results/backups/"$reference_res_base_filename"_bck_`date +%G%m%d%H%M%s`"
	echo "test results already exist, backing up as $bck_fn"
	cp $reference_res_filename $bck_fn
fi

#run test
echo "running test $curr_fio_test_filename, recording results in $reference_res_filename"
fio $curr_fio_test_filename > $reference_res_filename

