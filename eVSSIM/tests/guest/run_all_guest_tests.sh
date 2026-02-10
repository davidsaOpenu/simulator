#!/bin/bash
sudo VSSIM_NEXTGEN_BUILD_SYSTEM=1 nosetests -v --with-xunit --xunit-file=guest_tests_results.xml
NOSETESTS_RC=$?

# Run DoD tests (namespace discovery, sector R/W, object R/W)
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
"$SCRIPT_DIR/run_dod_tests.sh"
DOD_RC=$?

# Fail if either test suite failed
if [ $NOSETESTS_RC -ne 0 ] || [ $DOD_RC -ne 0 ]; then
    exit 1
fi
exit 0
