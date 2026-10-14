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

# ssh into the guest with a hard timeout (a kernel oops must not hang CI)
exofs_guest_run() {
    local seconds="$1"
    shift
    timeout --foreground "$seconds" ssh -q -o ConnectionAttempts=30 -o ServerAliveInterval=15 \
        -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no -o PasswordAuthentication=no \
        -o PubkeyAcceptedKeyTypes=+ssh-rsa,ssh-ed25519 \
        -i "$EVSSIM_SSH_KEYS_PATH/id_ed25519" -i "$EVSSIM_SSH_KEYS_PATH/id_rsa" \
        -p "$EVSSIM_QEMU_SSH_PORT" "$EVSSIM_QEMU_UBUNTU_USERNAME@localhost" "$@"
}

# evssim_qemu_stop (builder.sh) runs "docker ps | grep <id>" twice under
# set -e/pipefail; when the container exits by itself between the two, the
# grep fails and aborts the run although everything passed. Stopping qemu
# must never fail the run.
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
# Step 1b: fresh guest image + latest copy of the exofs tests
# ---------------------------------------------------------------------------
info "preparing a fresh guest image from $EXOFS_IMAGE_TEMPLATE"
cp -f "$EXOFS_IMAGE_TEMPLATE" "$EXOFS_IMAGE"
evssim_run "$EXOFS_GUEST_CONTAINER" "sudo guestfish -a '$DROOT/$DIST/$EVSSIM_QEMU_IMAGE' -i <<'GFEOF'
mkdir-p $EVSSIM_GUEST_HOME_PATH/guest
copy-in $DROOT/$EVSSIM_SIMULATOR_FOLDER/eVSSIM/tests/guest/exofs $EVSSIM_GUEST_HOME_PATH/guest
command \"chown -R $EVSSIM_QEMU_UBUNTU_USERNAME:$EVSSIM_QEMU_UBUNTU_USERNAME $EVSSIM_GUEST_HOME_PATH/guest\"
GFEOF"

# ---------------------------------------------------------------------------
# Step 2: boot the VM on the project qemu (kernel $krel, 2 object-mode drives)
# ---------------------------------------------------------------------------
info "booting Ubuntu 14.04 with kernel $krel (storage strategy $EVSSIM_RUNTIME_STORAGE_STRATEGY)"
if [ "$mode" = "attached" ]; then
    evssim_qemu_attached "$EXOFS_GUEST_CONTAINER"
    exit 0
fi
evssim_qemu_detached "$EXOFS_GUEST_CONTAINER"   # waits for ssh, stops qemu on exit
evssim_wait_for_device /dev/nvme0n1 120
evssim_wait_for_device /dev/nvme1n1 60

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
        ssh -t -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no -o PasswordAuthentication=no \
            -o PubkeyAcceptedKeyTypes=+ssh-rsa,ssh-ed25519 \
            -i "$EVSSIM_SSH_KEYS_PATH/id_ed25519" -i "$EVSSIM_SSH_KEYS_PATH/id_rsa" \
            -p "$EVSSIM_QEMU_SSH_PORT" "$EVSSIM_QEMU_UBUNTU_USERNAME@localhost" || true
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
     EXOFS_MANY_FILES=$EXOFS_MANY_FILES EXOFS_EXPECTED_KERNEL=$krel \
     EVSSIM_PROVISIONED_DEVICE_COUNT=${EVSSIM_PROVISIONED_DEVICE_COUNT:-2} \
     bash $GUEST_TEST_DIR/run_exofs_tests.sh $EXOFS_TESTS"
test_rc=$?
set -e
[ $test_rc -ne 124 ] || echo "ERROR [exofs-run] tests timed out after ${EXOFS_TEST_TIMEOUT}s (guest hang / kernel oops?)"

# Collect results
mkdir -p "$LOG_DIR"
set +e
evssim_copy_from_guest "$GUEST_OUTPUT_DIR/*" "$LOG_DIR/" >/dev/null 2>&1 || \
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
