#!/bin/bash

valgrind --leak-check=full --error-exitcode=2 ./host_tests_main --ci
exit $?
