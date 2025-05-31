#!/bin/bash

set -e

valgrind --leak-check=full --error-exitcode=2 ./simulation_tests_main --ci
valgrind --leak-check=full --error-exitcode=2 ./unit_tests_main --ci
