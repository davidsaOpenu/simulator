#!/bin/bash

status=0
for tt in 'write' 'rw' 'read' ; do
	echo "RECORDING REFERENCE RESULTS FOR $tt TEST"
	./perf_test_rec_results.sh "test_"$tt || status=$?
done

if [ "$status" -eq 0 ]; then
    echo "ALL DONE"
fi
exit "$status"
