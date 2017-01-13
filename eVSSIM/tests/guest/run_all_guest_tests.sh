#!/bin/bash

pytest -v --junit-xml=./guest_tests_results.xml --capture=sys --ignore=objects_via_ioctl
exit $?
