#!/bin/bash


RESULT=0

# run sector tests
valgrind --leak-check=full --error-exitcode=2 ./sector_tests --ci
CURR_RESULT=$?

# run object tests
valgrind --leak-check=full --error-exitcode=2 ./object_tests
CURR_RESULT=$?
if [ $CURR_RESULT -ne 0 ]; then
  RESULT=1
fi

# run log manager tests
# (TODO: s/./account_mgr_tests/./log_mgr_tests)
valgrind --leak-check=full --error-exitcode=2 ./account_mgr_tests
CURR_RESULT=$?
if [ $CURR_RESULT -ne 0 ]; then
  RESULT=1
fi

exit $RESULT


