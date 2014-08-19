#!/bin/bash

for tt in 'write' 'rw' 'read' ; do
	echo "RUNNING $tt TEST"
	./perf_test.sh "test_"$tt"_short"
done

echo "ALL DONE"

