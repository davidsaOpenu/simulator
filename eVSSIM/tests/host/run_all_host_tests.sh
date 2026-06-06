#!/bin/bash

set -e

simulation_slices=(
	--sector-tests
	--object-tests
	--log-mgr-tests
	--ssd-io-emulator-tests
	--offline_logger_tests
	--ssd_write_read_test
	--ssd_program_compatible_test
	--onfi_ops_test
	--gc_tests
	--multi_device_tests
)

for simulation_slice in "${simulation_slices[@]}"; do
	valgrind --leak-check=full --error-exitcode=2 ./simulation_tests_main --ci "$simulation_slice"
done

valgrind --leak-check=full --error-exitcode=2 ./unit_tests_main --ci
