#!/bin/bash
set -Eeo pipefail

log() {
    echo "[test_on_podman_VM] $*"
}

trap 'ec=$?; log "FAILED with exit $ec on: $BASH_COMMAND" >&2' ERR

IMAGE_NAME="ubuntu-24.04-server-cloudimg-amd64-podman.img"

# ================== WORKSPACE & DIRECTORIES ==================

# Jenkins sets WORKSPACE; original inline script ran from there.
if [[ -n "${WORKSPACE:-}" ]]; then
    WS="$WORKSPACE"
    log "Detected Jenkins WORKSPACE: $WS"
else
    WS="$(pwd)"
    log "WORKSPACE not set; using current directory as workspace: $WS"
fi

cd "$WS"

# ================== PREPARE VM IMAGE ==================

# Optional download:
# wget --no-proxy "http://192.114.0.189/$IMAGE_NAME"

log "Copying image from /home/davidsa/public_html/$IMAGE_NAME to workspace..."
cp -f "/home/davidsa/public_html/$IMAGE_NAME" "$WS/"
# the logs folder shipped below outgrows the stock image, and an Elasticsearch
# node over its disk watermark cannot allocate .security-7, so the cluster comes
# up red and the built-in user passwords never get set
qemu-img resize "$WS/$IMAGE_NAME" +20G
log "Image ready at: $WS/$IMAGE_NAME"

# ================== PORT SELECTION ==================

find_free_port() {
    local port
    for port in {2224..2299}; do
        if ! netstat -tuln 2>/dev/null | grep -q ":$port "; then
            echo "$port"
            return
        fi
    done
    echo 2224  # fallback port
}

free_tcp_port="$(find_free_port)"
log "Using port: $free_tcp_port"

# Store variables for cleanup stage (same filenames as before)
echo "$free_tcp_port" > vm_port.txt
echo "$IMAGE_NAME"    > vm_image.txt

# ================== START QEMU VM ==================

log "Starting QEMU VM..."
qemu-system-x86_64 \
    -enable-kvm \
    -cpu host \
    -m 4096 \
    -smp 4 \
    -hda "$IMAGE_NAME" \
    -nic user,hostfwd=tcp::"$free_tcp_port"-:22 \
    -nographic &

VM_PID=$!
echo "$VM_PID" > vm_pid.txt
log "Started VM with PID: $VM_PID"

sleep 10

if ! kill -0 "$VM_PID" 2>/dev/null; then
    echo "ERROR: VM process failed to start or crashed immediately"
    echo "VM PID $VM_PID is not running"
    exit 1
fi
log "VM process is running, waiting for SSH connectivity..."

# ================== WAIT FOR SSH ==================

log "Waiting for VM to be ready (SSH on port $free_tcp_port)..."
ready=0
for i in {1..60}; do
    if ssh -o ConnectTimeout=5 -o StrictHostKeyChecking=no \
          -p "$free_tcp_port" elk@localhost "echo VM is ready" 2>/dev/null; then
        log "VM is ready after attempt $i"
        ready=1
        break
    fi
    log "Attempt $i: VM not ready yet, waiting..."
    sleep 10
done

if [[ "$ready" -ne 1 ]]; then
    echo "ERROR: VM did not become ready in time (SSH not reachable)"
    exit 1
fi

# ================== COPY simulator & logs ==================

log "Copying simulator folder to VM..."
ssh -o StrictHostKeyChecking=no -p "$free_tcp_port" elk@localhost "rm -rf ~/simulator ~/logs" || true
scp -o StrictHostKeyChecking=no -P "$free_tcp_port" -r simulator elk@localhost:~/simulator

if [[ -d logs ]]; then
    log "Copying logs folder to VM..."
    scp -o StrictHostKeyChecking=no -P "$free_tcp_port" -r logs elk@localhost:~/logs
else
    log "WARNING: logs folder not found in $WS; continuing without logs"
fi

log "Listing files on VM (ls -R)..."
ssh -o StrictHostKeyChecking=no -p "$free_tcp_port" elk@localhost "ls -R"

# ================== TEST PODMAN ==================

echo "Testing podman on VM..."
echo "==================== PODMAN OUTPUT ===================="
ssh -o StrictHostKeyChecking=no -p "$free_tcp_port" elk@localhost "podman run hello-world" \
    || echo "Podman command failed"
echo "======================= END OUTPUT ====================="

# ================== DISK REPORT ==================

# the ELK images and the shipped logs both land here; a node over the Elasticsearch
# disk watermark cannot allocate .security-7 and the cluster comes up red
log "Disk state before starting ELK:"
ssh -o StrictHostKeyChecking=no -p "$free_tcp_port" elk@localhost "df -h; lsblk" || true

# ================== START ELK ==================

echo "==================== START ELK ======================"
ssh -o StrictHostKeyChecking=no -p "$free_tcp_port" elk@localhost \
    "cd ~/simulator/infra/ELK/; ./install_and_start_elk.sh ../../../logs ../ELK"
echo "======================= END OUTPUT ====================="

# ================== HOST SIMULATION TESTS (ELK metrics) ==================

log "Extracting host-test runtime libraries from the evssim image..."
rm -rf "$WS/vm_rtlibs"
mkdir -p "$WS/vm_rtlibs"
docker run --rm --entrypoint bash \
    -v "$WS/simulator":/code/simulator -v "$WS/vm_rtlibs":/rtlibs evssim \
    -c 'ldd /code/simulator/eVSSIM/tests/host/simulation/simulation_tests_main | grep -oE "=> /[^ ]+" | cut -d" " -f2 | xargs -I{} cp -L {} /rtlibs/ && chmod 644 /rtlibs/*'
scp -o StrictHostKeyChecking=no -P "$free_tcp_port" -r "$WS/vm_rtlibs" elk@localhost:vm_rtlibs

log "Loading ubuntu:14.04 runtime base into VM podman..."
docker image inspect ubuntu:14.04 >/dev/null 2>&1 || docker pull ubuntu:14.04
docker save ubuntu:14.04 | ssh -o StrictHostKeyChecking=no -p "$free_tcp_port" elk@localhost "podman load"

echo "==================== HOST SIMULATION TESTS OUTPUT ===================="
ssh -o StrictHostKeyChecking=no -p "$free_tcp_port" elk@localhost bash -s <<'REMOTE'
set -Eeo pipefail
mkdir -p ~/logs ~/data
cd ~/simulator/infra/ELK
set -o allexport
source ./.env
source ./host_tests_bounds.env
set +o allexport
source ./elk_run_metrics.sh
rm -f ~/logs/*.log
elk_wipe
podman run --rm \
    -v ~/simulator:/code/simulator \
    -v ~/logs:/code/logs \
    -v ~/data:/code/data \
    -v ~/vm_rtlibs:/rtlibs \
    -e LD_LIBRARY_PATH=/rtlibs \
    -w /code/simulator/eVSSIM/tests/host/simulation \
    docker.io/library/ubuntu:14.04 ./simulation_tests_main --object-tests
elk_settle
metrics="$(elk_query_metrics)"
echo "$metrics"
elk_assert_case object_tests "$metrics"
REMOTE
echo "======================= END OUTPUT ====================="

log "test_on_podman_VM.sh completed successfully."
