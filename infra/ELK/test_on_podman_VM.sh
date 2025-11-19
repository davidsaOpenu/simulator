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

# ================== START ELK ==================

echo "==================== START ELK ======================"
ssh -o StrictHostKeyChecking=no -p "$free_tcp_port" elk@localhost \
    "cd ~/simulator/infra/ELK/; ./install_and_start_elk.sh ../../../logs ../ELK"
echo "======================= END OUTPUT ====================="

log "test_on_podman_VM.sh completed successfully."
