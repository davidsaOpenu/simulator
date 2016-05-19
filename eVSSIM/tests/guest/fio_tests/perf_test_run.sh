#!/bin/bash

for tt in 'write' 'rw' 'read' ; do
	echo "RUNNING $tt TEST"
	./perf_test.sh "test_"$tt
done

echo "ALL DONE"

