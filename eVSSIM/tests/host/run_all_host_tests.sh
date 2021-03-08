#!/bin/bash


RESULT=0

# run sector tests
valgrind --leak-check=full --error-exitcode=2 ./host_tests_main --sector-tests
CURR_RESULT=$?

# run object tests
valgrind --leak-check=full --error-exitcode=2 ./host_tests_main --object-tests
CURR_RESULT=$?
if [ $CURR_RESULT -ne 0 ]; then
  RESULT=1
fi

# run log manager tests
valgrind --leak-check=full --suppressions=./log_mgr_tests.supp --error-exitcode=2 ./host_tests_main --log-mgr-tests
CURR_RESULT=$?
if [ $CURR_RESULT -ne 0 ]; then
  RESULT=1
fi


# run io emulator monitor statistics tests
# performance tests that incur real delays must run naively
./host_tests_main --ssd-io-emulator-tests
CURR_RESULT=$?
if [ $CURR_RESULT -ne 0 ]; then
  RESULT=1
fi

exit $RESULT
