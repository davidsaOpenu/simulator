#!/bin/bash
source ./builder.sh

# Run the host test suite — this is the specific, fixed traffic workload
# that the ELK performance test measures.
# The caller (run-ci.sh) records the start timestamp and passes it to
# elk_performance_test.sh via START_AT to constrain the query window.
evssim_run_at_folder $EVSSIM_SIMULATOR_FOLDER/eVSSIM/tests/host ./run_all_host_tests.sh
