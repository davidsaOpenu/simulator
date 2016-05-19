#!/bin/bash

for tt in 'write' 'rw' 'read' ; do
	echo "RECORDING REFERENCE RESULTS FOR $tt TEST"
	./perf_test_rec_results.sh "test_"$tt"_short"
done

echo "ALL DONE"

