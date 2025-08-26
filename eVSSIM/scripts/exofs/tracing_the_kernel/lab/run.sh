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
OUTPUT_DIR=${OUTPUT_DIR:-output}
mkdir -p "$OUTPUT_DIR"
make clean
make
rm -rf /mnt/exofs0/my_file /tmp/my_log.tmp
head -c 4096 /dev/urandom > /mnt/exofs0/my_file

# prepare to trace
echo function_graph > /sys/kernel/debug/tracing/current_tracer # trace all functions

# filter the following
echo > /sys/kernel/debug/tracing/set_ftrace_filter
echo > "/sys/kernel/debug/tracing/set_graph_function"
echo "__x64_sys_$1" > /sys/kernel/debug/tracing/set_graph_function
echo > /sys/kernel/debug/tracing/trace
# do some file operations here
dmesg -c
./test $1 cache_$2
dmesg -c > "$OUTPUT_DIR/$1_dmesg.txt"
cat /sys/kernel/debug/tracing/trace > /tmp/my_log.tmp
echo > /sys/kernel/debug/tracing/trace
echo nop > /sys/kernel/debug/tracing/current_tracer

# generate output
cut /tmp/my_log.tmp -c 23- | tail -n +5 > "$OUTPUT_DIR/$1.txt"

