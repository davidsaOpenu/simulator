#!/bin/bash


RESULT=0

# run sector tests
valgrind --leak-check=full --error-exitcode=2 ./sector_tests --ci
CURR_RESULT=$?

# run object tests
# TODO: uncomment when running time issue is fixed
valgrind --leak-check=full --error-exitcode=2 ./object_tests
CURR_RESULT=$?
if [ $CURR_RESULT -ne 0 ]; then
  RESULT=1
fi

# run log manager tests
valgrind --leak-check=full --suppressions=./log_mgr_tests.supp --error-exitcode=2 ./log_mgr_tests
CURR_RESULT=$?
if [ $CURR_RESULT -ne 0 ]; then
  RESULT=1
fi


# run io emulator monitor statistics tests
# performance tests that incur real delays must run naively
./ssd_io_emulator_tests
CURR_RESULT=$?
if [ $CURR_RESULT -ne 0 ]; then
  RESULT=1
fi

./offline_logger_tests
CURR_RESULT=$?
if [ $CURR_RESULT -ne 0 ]; then
  RESULT=1
fi

exit $RESULT


