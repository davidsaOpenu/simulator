#!/bin/bash

for tt in 'write' 'rw' 'read' ; do
	echo "RECORDING REFERENCE RESULTS FOR $tt TEST"
	./perf_test_rec_results.sh "test_"$tt
done

echo "ALL DONE"

