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

# The base image is used from IMAGE_DIR when it is there (central's own
# public_html); anywhere else it is downloaded once from central into a
# per-user cache and reused by later runs.
IMAGE_DIR="${IMAGE_DIR:-/home/davidsa/public_html}"
IMAGE_CACHE_DIR="${IMAGE_CACHE_DIR:-$HOME/.cache/evssim-images}"
IMAGE_URL="${IMAGE_URL:-https://192.114.0.189:8080/$IMAGE_NAME}"
# Central's certificate is self-signed: pass its copy to verify it
IMAGE_CA_CERT="${IMAGE_CA_CERT:-}"

if [[ -f "$IMAGE_DIR/$IMAGE_NAME" ]]; then
    BASE_IMAGE="$IMAGE_DIR/$IMAGE_NAME"
    log "Using image from $BASE_IMAGE"
elif [[ -f "$IMAGE_CACHE_DIR/$IMAGE_NAME" ]]; then
    BASE_IMAGE="$IMAGE_CACHE_DIR/$IMAGE_NAME"
    log "Using cached image $BASE_IMAGE"
else
    BASE_IMAGE="$IMAGE_CACHE_DIR/$IMAGE_NAME"
    if [[ -n "$IMAGE_CA_CERT" ]]; then
        wget_tls=(--ca-certificate="$IMAGE_CA_CERT")
    else
        log "WARNING: IMAGE_CA_CERT not set; downloading without verifying the certificate"
        wget_tls=(--no-check-certificate)
    fi
    log "Image not found in $IMAGE_DIR; downloading $IMAGE_URL to $BASE_IMAGE..."
    mkdir -p "$IMAGE_CACHE_DIR"
    # Download to a temporary name so an interrupted run never leaves a partial image behind
    wget --no-proxy -nv "${wget_tls[@]}" -O "$BASE_IMAGE.part" "$IMAGE_URL"
    mv -f "$BASE_IMAGE.part" "$BASE_IMAGE"
fi

# Boot a throwaway overlay so the base image is never written to
if [[ "$WS/$IMAGE_NAME" -ef "$BASE_IMAGE" ]]; then
    echo "ERROR: workspace $WS holds the base image itself; run from another directory"
    exit 1
fi
rm -f "$WS/$IMAGE_NAME"
qemu-img create -q -f qcow2 -F qcow2 -b "$BASE_IMAGE" "$WS/$IMAGE_NAME"
log "Image ready at: $WS/$IMAGE_NAME (overlay on $BASE_IMAGE)"

# ================== SSH KEY ==================

VM_USER=elk

# Use the running user's keys; create one if there is none
shopt -s nullglob
pub_keys=("$HOME"/.ssh/*.pub)
shopt -u nullglob
if [[ ${#pub_keys[@]} -eq 0 ]]; then
    log "No SSH key in $HOME/.ssh; generating $HOME/.ssh/id_ed25519"
    mkdir -p -m 700 "$HOME/.ssh"
    ssh-keygen -q -t ed25519 -N "" -f "$HOME/.ssh/id_ed25519"
    pub_keys=("$HOME/.ssh/id_ed25519.pub")
fi

SSH_OPTS=(-o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR)
for pub in "${pub_keys[@]}"; do
    [[ -f "${pub%.pub}" ]] && SSH_OPTS+=(-i "${pub%.pub}")
done
log "SSH keys: ${pub_keys[*]}"

# Inject the keys through a cloud-init NoCloud seed: libguestfs can't open the
# image as a non-root user on Ubuntu (the host kernel is root-only). A fresh
# instance-id makes cloud-init apply it even though the image booted before.
SEED_DIR="$WS/vm_seed"
SEED_ISO="$WS/vm_seed.iso"
rm -rf "$SEED_DIR"
mkdir -p "$SEED_DIR"
{
    echo "#cloud-config"
    echo "write_files:"
    echo "  - path: /run/evssim_authorized_keys"
    echo "    permissions: '0600'"
    echo "    content: |"
    cat "${pub_keys[@]}" | sed 's/^/      /'
    echo "runcmd:"
    echo "  - [sh, -c, 'd=\$(getent passwd $VM_USER | cut -d: -f6)/.ssh; f=\$d/authorized_keys; install -d -m 700 -o $VM_USER -g $VM_USER \$d; touch \$f; while read -r k; do [ -n \"\$k\" ] && ! grep -qxF \"\$k\" \$f && echo \"\$k\" >> \$f; done < /run/evssim_authorized_keys; chown $VM_USER:$VM_USER \$f; chmod 600 \$f']"
} > "$SEED_DIR/user-data"
printf 'instance-id: evssim-%s-%s\n' "$(date +%s)" "$$" > "$SEED_DIR/meta-data"
genisoimage -quiet -output "$SEED_ISO" -volid cidata -joliet -rock "$SEED_DIR/user-data" "$SEED_DIR/meta-data"
log "cloud-init seed ready at: $SEED_ISO"

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
    -cdrom "$SEED_ISO" \
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
    if ssh "${SSH_OPTS[@]}" -o ConnectTimeout=5 \
          -p "$free_tcp_port" "$VM_USER@localhost" "echo VM is ready" 2>/dev/null; then
        log "VM is ready after attempt $i"
        ready=1
        break
    fi
    log "Attempt $i: VM not ready yet, waiting..."
    sleep 10
done

if [[ "$ready" -ne 1 ]]; then
    echo "ERROR: VM did not become ready in time (SSH not reachable with keys: ${pub_keys[*]})"
    exit 1
fi

# ================== COPY simulator & logs ==================

log "Copying simulator folder to VM..."
ssh "${SSH_OPTS[@]}" -p "$free_tcp_port" "$VM_USER@localhost" "rm -rf ~/simulator ~/logs" || true
scp "${SSH_OPTS[@]}" -P "$free_tcp_port" -r simulator "$VM_USER@localhost":~/simulator

if [[ -d logs ]]; then
    log "Copying logs folder to VM..."
    scp "${SSH_OPTS[@]}" -P "$free_tcp_port" -r logs "$VM_USER@localhost":~/logs
else
    log "WARNING: logs folder not found in $WS; continuing without logs"
fi

log "Listing files on VM (ls -R)..."
ssh "${SSH_OPTS[@]}" -p "$free_tcp_port" "$VM_USER@localhost" "ls -R"

# ================== TEST PODMAN ==================

echo "Testing podman on VM..."
echo "==================== PODMAN OUTPUT ===================="
ssh "${SSH_OPTS[@]}" -p "$free_tcp_port" "$VM_USER@localhost" "podman run hello-world" \
    || echo "Podman command failed"
echo "======================= END OUTPUT ====================="

# ================== START ELK ==================

echo "==================== START ELK ======================"
ssh "${SSH_OPTS[@]}" -p "$free_tcp_port" "$VM_USER@localhost" \
    "cd ~/simulator/infra/ELK/; ./install_and_start_elk.sh ../../../logs ../ELK"
echo "======================= END OUTPUT ====================="

log "test_on_podman_VM.sh completed successfully."
