#!/bin/bash

if [[ "$#" -ne 2 ]]; then
    echo "Usage: $0 <operation> <cache>"
    exit 1
fi

case "$1" in
    open|close|read|write|mount|umount)
        ;;
    *)
        echo "Invalid operation: $1"
        echo "Supported operations: open, close, read, write, mount, umount"
        exit 1
        ;;
esac

case "$2" in
    enabled|disabled)
        ;;
    *)
        echo "Invalid operation: $2"
        echo "Supported operations: enabled, disabled"
        exit 1
        ;;
esac

# test a specific operation on exofs

# pre-requirements
mkdir -p output
make clean
make
rm -rf /mnt/osd0/my_file /tmp/my_log.tmp
head -c 4096 /dev/urandom > /mnt/osd0/my_file

# prepare to trace
echo 1 > /sys/kernel/debug/tracing/tracing_on # enable tracing
echo > /sys/kernel/debug/tracing/set_ftrace_pid
echo function_graph > /sys/kernel/debug/tracing/current_tracer # trace all functions

# filter the following
echo > /sys/kernel/debug/tracing/set_ftrace_filter
echo SyS_$1 > /sys/kernel/debug/tracing/set_graph_function

# start the tracing
echo > /sys/kernel/debug/tracing/trace # clear the tracing buffer

# do some file operations here
./test $1 cache_$2 &
pid=$!

sleep 1

# end tracing
echo 0 > /sys/kernel/debug/tracing/tracing_on # stop tracing
echo > /sys/kernel/debug/tracing/set_ftrace_filter
cat /sys/kernel/debug/tracing/trace > /tmp/my_log.tmp
echo nop > /sys/kernel/debug/tracing/current_tracer

# generate output
cut /tmp/my_log.tmp -c 23- | tail -n +5 > output/$1.txt

