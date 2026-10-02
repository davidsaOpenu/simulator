#!/bin/bash

set -e

VALGRIND="valgrind --leak-check=full --error-exitcode=2"

$VALGRIND ./unit_tests_main --ci
# heavy suites run natively: valgrind at their event volume blows the CI timeout (WriteReadTest 4h19 vs 18min)
$VALGRIND ./simulation_tests_main --ci --gtest_filter=*-*WriteReadTest*:*OnfiCommandsTest*
./simulation_tests_main --ci --ssd_write_read_test
./simulation_tests_main --ci --onfi_ops_test
$VALGRIND ./functional_tests_main --ci
