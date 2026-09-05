#!/bin/bash
source ./builder.sh

evssim_validate_version_arguments "$0" "${1:-}" "$#"
version="$1"

internal_image_path="$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/$EVSSIM_QEMU_IMAGE"

guest_test() {
    local output=$1
    local strategy=$2
    local simulator=$3
    local test_name=$4
    local required_events=${5:-}

    test_index=$(($test_index+1))

    # Make a fresh copy
    evssim_qemu_fresh_image "$version"

    local realtime_delay=${EVSSIM_RUNTIME_REALTIME_DELAY:-0}
    local realtime_mode=off
    [ "$realtime_delay" = 1 ] && realtime_mode=on

    # Run qemu with test specific configuration
    EVSSIM_QEMU_TRACE_EVENTS="$required_events" EVSSIM_RUNTIME_STORAGE_STRATEGY=$strategy EVSSIM_QEMU_SIMULATOR_ENABLED=$simulator evssim_qemu_detached "$version"

    # The container is --rm and the real-time delay report is printed at
    # shutdown, so follow its log from the start.
    local qemu_log
    qemu_log=$(mktemp)
    docker logs -f "$EVSSIM_DOCKER_UUID" > "$qemu_log" 2>&1 &
    local log_pid=$!

    # Wait for the NVMe device to appear before running tests.
    evssim_wait_for_device /dev/nvme0n1

    # Every device must report the REALTIME_DELAY mode it was given. The line
    # prints either way, so it is checked against the flag, not for presence.
    local mode_lines=""
    local mode_rc=0
    if [ "$simulator" = yes ]; then
        mode_lines=$(docker logs "$EVSSIM_DOCKER_UUID" 2>&1 | grep -E "realtime delay: (on|off)" || true)
        if [ -z "$mode_lines" ] || echo "$mode_lines" | grep -qv "realtime delay: $realtime_mode"; then
            echo "ERROR Simulator did not report 'realtime delay: $realtime_mode' for every device:"
            echo "${mode_lines:-<none>}"
            mode_rc=1
        fi
    fi

    # Run tests inside the guest
    echo "INFO Running test id=$test_index strategy=$strategy simulator=$simulator test=$test_name"
    set +e
    evssim_guest "cd ./guest; mkdir Logs; sudo VSSIM_NEXTGEN_BUILD_SYSTEM=1 EVSSIM_PROVISIONED_DEVICE_COUNT=$EVSSIM_PROVISIONED_DEVICE_COUNT EVSSIM_RUNTIME_REALTIME_DELAY=$realtime_delay nosetests -v -s --with-xunit --xunit-file=guest_tests_results.xml $test_name"
    test_rc=$?
    set -e
    [ $mode_rc -ne 0 ] && test_rc=1

    # Fail if a required trace event never fired; QEMU's log is lost once it stops
    for event in $required_events; do
        count=$(docker logs "$EVSSIM_DOCKER_UUID" 2>&1 | grep -c ":$event " || true)
        echo "INFO $event fired $count times"
        if [ "$count" -eq 0 ]; then
            echo "ERROR Required QEMU trace event $event never fired"
            test_rc=1
        fi
    done

    if ! docker ps -q --no-trunc | grep -q "$EVSSIM_DOCKER_UUID"; then
        echo "ERROR QEMU container died during the run"
        test_rc=1
    fi

    # Stop qemu and wait
    evssim_qemu_flush_disk
    evssim_qemu_stop

    # SSD_IO_TERM prints one report per device with the flag on
    wait "$log_pid" 2>/dev/null || true
    if [ "$simulator" = yes ] && [ "$realtime_delay" = 1 ]; then
        local reports
        reports=$(grep "realtime delay report:" "$qemu_log" || true)
        echo "${reports:-INFO No real-time delay report}"
        if [ -z "$reports" ]; then
            echo "ERROR No real-time delay report at shutdown"
            test_rc=1
        fi
    fi
    rm -f "$qemu_log"

    # Create test directory and gather results
    test_directory=$test_directory_base/run-$test_index-strategy-$strategy-test-$test_name
    evssim_run "$version" "mkdir -p /tmp/guest-logs \
        && sudo virt-copy-out -a $internal_image_path -i $EVSSIM_GUEST_HOME_PATH/guest/Logs /tmp/guest-logs \
	&& mkdir -p $test_directory_base \
	&& mv /tmp/guest-logs/Logs $test_directory"

    # Fail if tests fails
    if [ $test_rc -ne 0 ]; then
        echo "ERROR Guest test failed with strategy=$strategy, test=$test_name, error=$test_rc"
        echo "ERROR See logs @ $test_directory"
        exit $test_rc
    fi
}

# Configure directory base
test_directory_base="$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_LOGS_FOLDER/tests/$(date +'%Y-%m-%d-%H-%M-%S')"
test_index=0

# Run disk tests
guest_test "$test_directory_base" 1 yes nvme_compliance_tests
guest_test "$test_directory_base" 1 no fio_tests
guest_test "$test_directory_base" 1 yes ext4
# The same ext4 fio workload with simulated flash time spent in real time, then
# without; the test asserts the matching time bound
EVSSIM_RUNTIME_REALTIME_DELAY=1 guest_test "$test_directory_base" 1 yes realtime_delay_tests
EVSSIM_RUNTIME_REALTIME_DELAY=0 guest_test "$test_directory_base" 1 yes realtime_delay_tests

# Run simulator specific tests (With different strategies)
guest_test "$test_directory_base" 2 yes objects_via_ioctl
guest_test "$test_directory_base" 2 yes objects_queue_reset nvme_del_sq
# Linux stops submitting 10-20 ms before it deletes a queue (an RCU grace
# period), so an object command only outlasts that with slow flash: a 50 ms
# program, with REALTIME_DELAY on so the guest waits for it
EVSSIM_RUNTIME_REALTIME_DELAY=1 EVSSIM_RUNTIME_CELL_PROGRAM_DELAY=50000 \
    guest_test "$test_directory_base" 2 yes objects_queue_reset "nvme_del_sq nvme_drain_kv"
# NOTE This is mock for future tests
#guest_test "$test_directory_base" 1 on simulator_test0
#guest_test "$test_directory_base" 2 on simulator_test0
