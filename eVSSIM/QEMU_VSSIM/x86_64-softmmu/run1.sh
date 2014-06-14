#!/bin/bash

set -x
echo "[args] $@"

dev=$1
addr=1.1.1.1/24

ip link set up dev $dev
ip addr add dev $dev $addr


