#!/bin/bash
source ./builder.sh

namespaces_test() {
    # Make a fresh copy
    evssim_qemu_fresh_image

    # Run qemu with strategy=2 (object) so the guest sees both a sector namespace
    # (nvme01, always strategy 1) and an object namespace (nvme02, strategy from env).
    EVSSIM_RUNTIME_STORAGE_STRATEGY=2 EVSSIM_QEMU_SIMULATOR_ENABLED=yes \
        evssim_qemu_detached

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
