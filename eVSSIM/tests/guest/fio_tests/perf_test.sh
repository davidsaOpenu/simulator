#!/bin/bash

if [ "$#" -ne 1 ]; then
    echo 'usage: perf_test test_name' >&2
    exit 1
fi

test_name=$1
reference_res_filename="./reference_results/${test_name}_ref_res"
curr_fio_test_filename="./fio_jobs/${test_name}.fio"
# Honor the environment's temporary directory, including TMPDIR in CI.
# Use the same Python 3 runtime as upstream's guest test suite.
result_dir=$(python3 -c 'import tempfile; print(tempfile.gettempdir())') || exit 1
curr_res_filename="${result_dir}/${test_name}_res"

# The job, rather than a missing reference metric, determines required coverage.
test_types=$(python3 ./fio_metrics.py directions "$curr_fio_test_filename") || exit 1
python3 ./fio_metrics.py validate "$curr_fio_test_filename" "$reference_res_filename" || exit 1

for test_type in $test_types; do
    # Preserve the historical invocation per direction, including both mixed
    # runs. A unique file retains each raw result before the last result is reused.
    if [ "$(grep -v '#filename' "$curr_fio_test_filename" | grep -c 'filename=/dev/nvme0n1')" -ne 1 ]; then
        echo "no active filename line in $curr_fio_test_filename" >&2
        exit 1
    fi
    test_size=$(grep 'size=' "$curr_fio_test_filename")
    test_size=${test_size//[!0-9]/}
    echo "zeroing ${test_size}m on /dev/nvme0n1"
    echo time dd if=/dev/zero of=/dev/nvme0n1 bs=1M count="$test_size"

    raw_result=$(mktemp "${result_dir}/${test_name}_${test_type}_res.XXXXXXXX") || exit 1
    echo "running $curr_fio_test_filename; raw result: $raw_result"
    fio "$curr_fio_test_filename" > "$raw_result" 2>&1
    status=$?
    # Retain the existing last-result path as well as the unique invocation.
    cp "$raw_result" "$curr_res_filename" || exit 1
    if [ "$status" -ne 0 ]; then
        echo "fio failed with status $status; output: $raw_result" >&2
        exit "$status"
    fi
    # Validate both metrics for every mixed invocation, not just the direction
    # whose comparison happens to be printed for this invocation.
    python3 ./fio_metrics.py compare "$curr_fio_test_filename" \
        "$reference_res_filename" "$raw_result" "$test_type" || exit 1
done
