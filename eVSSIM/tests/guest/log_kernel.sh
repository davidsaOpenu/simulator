#!/bin/bash
while true
do
	sudo dmesg -c
	sleep 1
done
exit $?
