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
# TODO: fix compilation warnings and valgrind complaints
valgrind --leak-check=full --error-exitcode=2 ./log_mgr_tests
./log_mgr_tests
CURR_RESULT=$?
if [ $CURR_RESULT -ne 0 ]; then
  RESULT=1
fi

# Monitor logging parse tests
./qt_monitor_log_parser_tests
CURR_RESULT=$?
if [ $CURR_RESULT -ne 0 ]; then
  RESULT=1
fi

# Monitor log calculation tests
./qt_monitor_tests
CURR_RESULT=$?
if [ $CURR_RESULT -ne 0 ]; then
  RESULT=1
fi

# Delays calculations tests
./delay_calc_tests
CURR_RESULT=$?
if [ $CURR_RESULT -ne 0 ]; then
  RESULT=1
fi
exit $RESULT


