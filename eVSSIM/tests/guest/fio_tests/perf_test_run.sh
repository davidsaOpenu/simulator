#!/bin/bash

status=0
for tt in 'write' 'rw' 'read' ; do
	echo "RUNNING $tt TEST"
	./perf_test.sh "test_"$tt || status=$?
done

if [ "$status" -eq 0 ]; then
    echo "ALL DONE"
fi
exit "$status"
