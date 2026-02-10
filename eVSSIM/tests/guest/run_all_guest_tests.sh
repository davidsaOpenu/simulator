#!/bin/bash
sudo VSSIM_NEXTGEN_BUILD_SYSTEM=1 nosetests -v --with-xunit --xunit-file=guest_tests_results.xml
exit $?
