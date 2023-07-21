#!/bin/bash
sudo nosetests -v --with-xunit --xunit-file=guest_tests_results.xml
exit $?
