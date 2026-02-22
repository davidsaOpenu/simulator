#!/bin/bash
source ./builder.sh

namespaces_test() {
    # Make a fresh copy
    evssim_qemu_fresh_image

    # Run qemu with simulator disabled to exercise namespace routing only
    EVSSIM_QEMU_SIMULATOR_ENABLED=no evssim_qemu_detached

    # Run tests inside the guest
    echo "INFO Running namespace tests"
    set +e
    evssim_guest "cd ./guest; mkdir Logs; sudo VSSIM_NEXTGEN_BUILD_SYSTEM=1 nosetests -v --with-xunit --xunit-file=guest_tests_results.xml namespace_tests"
    test_rc=$?
    set -e

    # Stop qemu and wait
    evssim_qemu_flush_disk
    evssim_qemu_stop

    # Fail if tests fail
    if [ $test_rc -ne 0 ]; then
        echo "ERROR namespace tests failed"
        exit $test_rc
    fi
}

namespaces_test "$@"
