#!/bin/bash

set -o pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

valgrind --leak-check=full --error-exitcode=2 ./simulation_tests_main | tee simulation_tests_main.log
status=$?

# Always print the timing summary, even if a suite above failed, so CI
# failures still surface how long each test/suite ran before the failure.
if [ $status -ne 0 ]; then
    awk -f "$SCRIPT_DIR/print_test_timing_summary.awk" simulation_tests_main.log || true
    exit $status
fi

valgrind --leak-check=full --error-exitcode=2 ./unit_tests_main | tee unit_tests_main.log
status=$?

awk -f "$SCRIPT_DIR/print_test_timing_summary.awk" simulation_tests_main.log unit_tests_main.log || true

exit $status
