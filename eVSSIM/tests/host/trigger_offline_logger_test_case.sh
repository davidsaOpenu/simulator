#!/bin/bash

usage_and_exit() {
    echo "USAGE: trigger_offline_logger_test_case.sh <pow> <mode>"
    echo "\t<pow>: Power of 2 of number of blocks [0..15]"
    echo "\t<mode>: 0=r,1=w,2=rw"
    exit 1
}


echo "$#"
if [ $# -ne 2 ]; then
    usage_and_exit
fi

pow=$1
mode=$2

if [ "$pow" -lt "0" ] || [ "$pow" -ge "16" ]; then
    usage_and_exit
fi

if [ "$mode" -lt "0" ] || [ "$mode" -gt "2" ]; then
    usage_and_exit
fi

test_case=`expr $pow \* 3 \+ $mode`
./offline_logger_tests --gtest_filter=*LoggerWriterPageWriteTest/$test_case
