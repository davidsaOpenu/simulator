#!/bin/bash
# Boot the Ubuntu 14.04 + kernel 7.0.2 guest on the project qemu (object
# strategy, 2 NVMe drives) and run the exofs guest tests in it.
#
# Usage:
#   ./exofs/run.sh            run the full exofs test suite, collect results
#   ./exofs/run.sh shell      boot the VM and open an interactive shell in it
#   ./exofs/run.sh boot       boot the VM, check kernel + drives, and stop
#
# Environment (see exofs/env-exofs.sh): EXOFS_STRICT=1 also runs the tests of
# known exofs issues, EXOFS_TESTS="test_02_exofs_api.py:TestRename" runs a
# subset, EXOFS_LARGE_FILE_MB / EXOFS_MANY_FILES size the stress tests.
#
# Results: $EVSSIM_ROOT_PATH/logs/exofs/<timestamp>/ (xunit xml, log, dmesg)
set -eo pipefail

EXOFS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$EXOFS_DIR/.."
# shellcheck disable=SC1091
source ./env.sh >/dev/null
# shellcheck disable=SC1091
source "$EXOFS_DIR/env-exofs.sh"
# shellcheck disable=SC1091
source ./builder.sh            # sets -eu and changes directory to EVSSIM_ROOT_PATH

mode="${1:-test}"
ROOT="$EVSSIM_ROOT_PATH"
DROOT="$EVSSIM_DOCKER_ROOT_PATH"
DIST="$EXOFS_DIST_FOLDER"
GUEST_TEST_DIR="$EVSSIM_GUEST_HOME_PATH/guest/exofs"
GUEST_OUTPUT_DIR=/tmp/exofs-results
LOG_DIR="$ROOT/$EVSSIM_LOGS_FOLDER/exofs/$(date +'%Y-%m-%d-%H-%M-%S')"

info() { echo "INFO [exofs-run] $*"; }
die() { echo "ERROR [exofs-run] $*" >&2; exit 1; }

# All guest ssh goes through these options, with a hard timeout on every call
# (a kernel oops must not hang CI). On the CI host the builder's ssh helpers
# (evssim_wait_for_guest, evssim_guest) never got through to this 14.04 guest
# (builds 141, 143: the wait failed for 300s, then evssim_guest hung until
# the job timeout), while ssh with exactly these options worked at once, so
# the builder helpers are not used once qemu runs.
exofs_ssh_opts=(-o BatchMode=yes -o ConnectTimeout=10 -o ServerAliveInterval=15
    -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no -o LogLevel=ERROR
    -o PubkeyAcceptedKeyTypes=+ssh-rsa,ssh-ed25519
    -i "$EVSSIM_SSH_KEYS_PATH/id_ed25519" -i "$EVSSIM_SSH_KEYS_PATH/id_rsa"
    -p "$EVSSIM_QEMU_SSH_PORT")
exofs_guest_run() {
    local seconds="$1"
    shift
    timeout --foreground "$seconds" ssh "${exofs_ssh_opts[@]}" "$EVSSIM_QEMU_UBUNTU_USERNAME@localhost" "$@"
}

# Stopping qemu must never fail the run (evssim_qemu_stop could abort a run
# that had passed when the container exited during the force-kill; fixed in
# builder.sh, kept here as a safety net).
exofs_qemu_stop() {
    evssim_qemu_stop || true
}

# ---------------------------------------------------------------------------
# Preconditions
# ---------------------------------------------------------------------------
[ -n "${EVSSIM_KERNEL_DIST:-}" ] && [ -f "$EXOFS_KERNEL_RELEASE_FILE" ] || \
    die "kernel not built, run ./exofs/build.sh first"
krel="$EVSSIM_KERNEL_DIST"
for f in "kernel/vmlinuz-$krel" "kernel/initrd.img-$krel" "$EVSSIM_QEMU_IMAGE.template"; do
    [ -f "$ROOT/$DIST/$f" ] || die "missing $ROOT/$DIST/$f, run ./exofs/build.sh"
done
[ -x "$ROOT/$EVSSIM_QEMU_FOLDER/x86_64-softmmu/qemu-system-x86_64" ] || \
    die "exofs qemu is not built ($ROOT/$EVSSIM_QEMU_FOLDER), run ./exofs/build.sh qemu"
docker image inspect "$EVSSIM_DOCKER_IMAGE_NAME:$EXOFS_GUEST_CONTAINER" >/dev/null 2>&1 || \
    die "docker image $EVSSIM_DOCKER_IMAGE_NAME:$EXOFS_GUEST_CONTAINER missing, run ./build-docker-image.sh"

# ---------------------------------------------------------------------------
# Step 1b: fresh guest image + the current kernel modules + latest copy of
# the exofs tests (the modules are refreshed on every run, so a rebuilt
# exofs.ko is used even when the image itself was built earlier)
# ---------------------------------------------------------------------------
info "preparing a fresh guest image on top of $EXOFS_IMAGE_TEMPLATE"
# qcow2 overlay instead of a full 1.2G copy: only the changes of this run are
# written, the template stays untouched (backing path relative to the overlay)
rm -f "$EXOFS_IMAGE"
evssim_run_at_folder "$EXOFS_GUEST_CONTAINER" "$DIST" \
    "$DROOT/$EVSSIM_QEMU_FOLDER/qemu-img create -q -f qcow2 \
     -o backing_file=$(basename "$EXOFS_IMAGE_TEMPLATE"),backing_fmt=qcow2 $EVSSIM_QEMU_IMAGE"
evssim_run "$EXOFS_GUEST_CONTAINER" "sudo guestfish -a '$DROOT/$DIST/$EVSSIM_QEMU_IMAGE' -i <<'GFEOF'
rm-rf /lib/modules/$krel
copy-in $DROOT/$DIST/kernel/lib/modules/$krel /lib/modules
mkdir-p $EVSSIM_GUEST_HOME_PATH/guest
copy-in $DROOT/$EVSSIM_SIMULATOR_FOLDER/eVSSIM/tests/guest/exofs $EVSSIM_GUEST_HOME_PATH/guest
command \"chown -R $EVSSIM_QEMU_UBUNTU_USERNAME:$EVSSIM_QEMU_UBUNTU_USERNAME $EVSSIM_GUEST_HOME_PATH/guest\"
GFEOF"

# ---------------------------------------------------------------------------
# Step 2: boot the VM on the project qemu (kernel $krel, 2 object-mode drives)
# ---------------------------------------------------------------------------
# When ssh does not come up, show why in the log (CI keeps nothing else):
# what listens on the port, one verbose ssh attempt, qemu's own output, and
# the guest's logs (their dates show whether the guest reached its root fs).
exofs_boot_failed() {
    echo "ERROR [exofs-run] VM not reachable over ssh on port $EVSSIM_QEMU_SSH_PORT, diagnostics:"
    echo "--- listeners on port $EVSSIM_QEMU_SSH_PORT:"
    (ss -Htlnp 2>/dev/null || netstat -tlnp 2>/dev/null) | grep -E "[:.]$EVSSIM_QEMU_SSH_PORT[[:space:]]" || echo "(none)"
    echo "--- ssh -v (last lines):"
    timeout 30 ssh -v "${exofs_ssh_opts[@]}" "$EVSSIM_QEMU_UBUNTU_USERNAME@localhost" true 2>&1 | tail -15 || true
    echo "--- qemu output:"
    docker logs --tail 30 "${EVSSIM_DOCKER_UUID:-none}" 2>&1 || true
    exofs_qemu_stop
    echo "--- guest boot log (kernel lines of the last boot, sshd, auth):"
    evssim_run "$EXOFS_GUEST_CONTAINER" "sudo guestfish -a '$DROOT/$DIST/$EVSSIM_QEMU_IMAGE' -i <<'GFEOF'
-sh \"grep -h -e 'Linux version' -e 'sshd' -e 'Out of memory' -e 'panic' -e 'eth0' -e 'dhclient' -e 'preauth' -e 'Accepted' /var/log/dmesg /var/log/syslog /var/log/kern.log /var/log/auth.log 2>/dev/null | tail -n 40\"
-sh \"ls -l --time-style=+%F_%T /var/log/dmesg /var/log/syslog /var/log/auth.log 2>&1\"
GFEOF" || true
}

# Wait up to EXOFS_SSH_WAIT seconds for the guest ssh, logging why it fails.
exofs_wait_ssh() {
    local start=$SECONDS err last=""
    info "waiting for the guest ssh on port $EVSSIM_QEMU_SSH_PORT (up to ${EXOFS_SSH_WAIT}s)"
    while [ $((SECONDS - start)) -lt "$EXOFS_SSH_WAIT" ]; do
        if err="$(exofs_guest_run 30 true 2>&1)"; then
            info "guest ssh up after $((SECONDS - start))s"
            return 0
        fi
        err="$(echo "$err" | tail -1)"
        [ "$err" = "$last" ] || info "guest ssh not up yet ($((SECONDS - start))s): ${err:-no message}"
        last="$err"
        sleep 5
    done
    return 1
}

# Wait until an NVMe device shows up in the guest.
exofs_wait_device() {
    local dev="$1" max="$2" start=$SECONDS
    until exofs_guest_run 30 "test -b $dev"; do
        [ $((SECONDS - start)) -lt "$max" ] || die "$dev did not show up in the guest within ${max}s"
        sleep 2
    done
    info "$dev ready after $((SECONDS - start))s"
}

info "booting Ubuntu 14.04 with kernel $krel (storage strategy $EVSSIM_RUNTIME_STORAGE_STRATEGY)"
if [ "$mode" = "attached" ]; then
    evssim_qemu_attached "$EXOFS_GUEST_CONTAINER"
    exit 0
fi
# evssim_qemu_detached = this + evssim_wait_for_guest (builder ssh, see above);
# evssim_qemu_default also stops qemu on exit
evssim_qemu_default detached "$EXOFS_GUEST_CONTAINER"
exofs_wait_ssh || { exofs_boot_failed; die "VM did not come up"; }
exofs_wait_device /dev/nvme0n1 120
exofs_wait_device /dev/nvme1n1 60

guest_krel="$(exofs_guest_run 60 uname -r | tr -d '\r')"
info "guest kernel: $guest_krel"
[ "$guest_krel" = "$krel" ] || die "guest booted kernel '$guest_krel', expected '$krel'"
exofs_guest_run 60 "ls -l /dev/nvme* && test -f /lib/modules/$krel/kernel/fs/exofs/exofs.ko && which nvme mkfs.exofs"

case "$mode" in
    boot)
        info "VM is up with kernel $krel and both NVMe drives - stopping"
        exofs_qemu_stop
        exit 0
        ;;
    shell)
        info "opening a shell in the guest (tests are in ~/guest/exofs). Exit the shell to stop the VM."
        ssh -t "${exofs_ssh_opts[@]}" "$EVSSIM_QEMU_UBUNTU_USERNAME@localhost" || true
        exofs_qemu_stop
        exit 0
        ;;
    test) ;;
    *) die "unknown mode '$mode' (use test|shell|boot|attached)" ;;
esac

# ---------------------------------------------------------------------------
# Steps 3-6: nvme feature, insmod, mkfs + mount, full exofs test suite
# (all performed by the guest side runner / test fixtures)
# ---------------------------------------------------------------------------
info "running exofs guest tests (strict=$EXOFS_STRICT, large=${EXOFS_LARGE_FILE_MB}MB, timeout=${EXOFS_TEST_TIMEOUT}s)"
set +e
exofs_guest_run "$EXOFS_TEST_TIMEOUT" \
    "sudo OUTPUT_DIR=$GUEST_OUTPUT_DIR EXOFS_STRICT=$EXOFS_STRICT EXOFS_LARGE_FILE_MB=$EXOFS_LARGE_FILE_MB \
     EXOFS_MANY_FILES=$EXOFS_MANY_FILES EXOFS_CHURN_CYCLES=$EXOFS_CHURN_CYCLES EXOFS_EXPECTED_KERNEL=$krel \
     EVSSIM_PROVISIONED_DEVICE_COUNT=${EVSSIM_PROVISIONED_DEVICE_COUNT:-2} \
     bash $GUEST_TEST_DIR/run_exofs_tests.sh $EXOFS_TESTS"
test_rc=$?
set -e
[ $test_rc -ne 124 ] || echo "ERROR [exofs-run] tests timed out after ${EXOFS_TEST_TIMEOUT}s (guest hang / kernel oops?)"

# Collect results
mkdir -p "$LOG_DIR"
set +e
# tar over our own ssh, not evssim_copy_from_guest (builder scp, see above)
exofs_guest_run 120 "sudo tar -C $GUEST_OUTPUT_DIR -cf - ." 2>/dev/null | tar -C "$LOG_DIR" -xf - || \
    exofs_guest_run 60 "dmesg" > "$LOG_DIR/dmesg-after.txt" 2>&1
cp "$ROOT/$EVSSIM_DATA_FOLDER/ssd.conf" "$LOG_DIR/" 2>/dev/null
exofs_guest_run 60 sync >/dev/null 2>&1
set -e
exofs_qemu_stop

info "results in $LOG_DIR"
if [ $test_rc -ne 0 ]; then
    echo "ERROR [exofs-run] exofs guest tests failed (rc=$test_rc), see $LOG_DIR"
    exit $test_rc
fi
info "exofs guest tests PASSED"
