#!/bin/bash
sudo nosetests -v --with-xunit --xunit-file=guest_tests_results.xml  --exclude==objects_via_ioctl
exit $?
