#!/bin/bash
# Acceptance test for the REALTIME_DELAY mode.
#
# Boots the guest twice, flag on then off, running the same ext4 fio workload.
# The guest half owns the timing assertion (eVSSIM/tests/guest/
# realtime_delay_tests); this half checks what the guest cannot see: that the
# generated ssd.conf carries the value asked for, that SSD_IO_INIT reported that
# mode (the guard against a silent compile-out), and that QEMU survived - a bad
# coroutine call aborts the process rather than failing anything in the guest.
#
# Usage: ./docker-test-realtime-delay.sh CONTAINER_VERSION
#        MODES=on|off|both selects which halves to run (default both).

source ./builder.sh

evssim_validate_version_arguments "$0" "${1:-}" "$#"
version="$1"
modes="${MODES:-both}"

# fio at one page per ~1ms plus garbage collection: generous, but bounded.
GUEST_TIMEOUT=${GUEST_TIMEOUT:-3600}
SSD_CONF="$EVSSIM_ROOT_PATH/$EVSSIM_DATA_FOLDER/ssd.conf"

overall_rc=0

# Read a key out of the first device section of the generated ssd.conf.
# Parameters
#  - key
conf_device_value() {
    awk -v key="$1" '
        /^\[/ { section = $0 }
        section == "[nvme01]" && $1 == key { print $2; exit }
    ' "$SSD_CONF"
}

# Parameters
#  - flag - 0 or 1
run_case() {
    local flag="$1"
    local mode=off
    [ "$flag" = 1 ] && mode=on
    local case_rc=0
    local guest_rc=0

    echo
    echo "############ REALTIME_DELAY=$flag ############"

    evssim_qemu_fresh_image "$version"

    EVSSIM_RUNTIME_REALTIME_DELAY=$flag \
    EVSSIM_RUNTIME_STORAGE_STRATEGY=1 \
    EVSSIM_QEMU_SIMULATOR_ENABLED=yes \
        evssim_qemu_detached "$version"

    # 1. The configuration the simulator was actually handed.
    local conf_value
    conf_value=$(conf_device_value REALTIME_DELAY)
    echo "CONF REALTIME_DELAY=$conf_value (expected $flag)"
    if [ "$conf_value" != "$flag" ]; then
        echo "FAIL: ssd.conf carries REALTIME_DELAY=$conf_value, expected $flag"
        case_rc=1
    fi
    if grep -A20 '^\[ns01\]' "$SSD_CONF" | grep -q REALTIME_DELAY; then
        echo "FAIL: REALTIME_DELAY leaked into a namespace section"
        case_rc=1
    fi

    # 2. The mode the simulator reports at init. If the real-time path were
    #    compiled out this line would still print, so it is checked against the
    #    flag and not merely for being present.
    local mode_line
    mode_line=$(docker logs "$EVSSIM_DOCKER_UUID" 2>&1 | grep -m1 "realtime delay:" || true)
    echo "LOG ${mode_line:-<none>}"
    if ! echo "$mode_line" | grep -q "realtime delay: $mode"; then
        echo "FAIL: SSD_IO_INIT did not report 'realtime delay: $mode'"
        case_rc=1
    fi

    evssim_wait_for_device /dev/nvme0n1

    # 3. The workload. -s keeps the measured times on stdout; nose otherwise
    #    only shows them when a test fails.
    set +e
    evssim_guest "cd ./guest; sudo MODE=$mode timeout $GUEST_TIMEOUT nosetests -v -s --with-xunit --xunit-file=guest_tests_results.xml realtime_delay_tests"
    guest_rc=$?
    set -e
    if [ $guest_rc -ne 0 ]; then
        echo "FAIL: guest test exited $guest_rc"
        case_rc=1
    fi

    # 4. QEMU still standing.
    if ! docker ps -q --no-trunc | grep -q "$EVSSIM_DOCKER_UUID"; then
        echo "FAIL: QEMU container died during the run"
        case_rc=1
    fi

    evssim_qemu_flush_disk
    evssim_qemu_stop

    if [ $case_rc -ne 0 ]; then
        echo "REALTIME_DELAY=$flag: FAIL"
        overall_rc=1
    else
        echo "REALTIME_DELAY=$flag: PASS"
    fi
}

case "$modes" in
    on)   run_case 1 ;;
    off)  run_case 0 ;;
    both) run_case 1; run_case 0 ;;
    *)    echo "MODES must be one of: on, off, both"; exit 2 ;;
esac

echo
if [ $overall_rc -eq 0 ]; then echo "RESULT: PASS"; else echo "RESULT: FAIL"; fi
exit $overall_rc
