#!/bin/bash
set -Eeo pipefail

log() {
    echo "[test_on_podman_VM] $*"
}

trap 'ec=$?; log "FAILED with exit $ec on: $BASH_COMMAND" >&2' ERR

# The checkout this script belongs to (<repo>/infra/ELK). Before any cd: the
# script's path can be relative to where it was started.
SIMULATOR_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

VM_USER=elk

# The prepared image lives in the images folder of the user running the
# test; each run boots a fresh copy of it
IMAGES_DIR="${IMAGES_DIR:-$HOME/images}"
BASE_IMAGE="$IMAGES_DIR/ubuntu_26_04.img"
IMAGE_NAME="$IMAGES_DIR/ubuntu_26_04_test.img"
CLOUD_IMAGE_URL="${CLOUD_IMAGE_URL:-https://cloud-images.ubuntu.com/releases/26.04/release/ubuntu-26.04-server-cloudimg-amd64.img}"
# Disk size of the prepared image: room for podman's and ELK's container images
IMAGE_SIZE="${IMAGE_SIZE:-20G}"
# Password for elk on the VM console; "random" makes virt-customize pick one
ELK_VM_PASSWORD="${ELK_VM_PASSWORD:-random}"

# ================== HOST PREREQUISITES ==================

# 1_install_prereqs.sh sets these up. Fail here, not minutes into a run.
if [[ ! -r /dev/kvm || ! -w /dev/kvm ]]; then
    echo "ERROR: no access to /dev/kvm; run: sudo usermod -aG kvm $USER, then log in again" >&2
    exit 1
fi

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

# Stop a VM an earlier run left behind (the Jenkinsfile's cleanup normally
# does), or it keeps running on the test image this run replaces
if [[ -f vm_pid.txt ]]; then
    old_pid="$(cat vm_pid.txt)"
    if [[ "$(ps -o comm= -p "$old_pid" 2>/dev/null)" == qemu-system-x86* ]]; then
        log "Stopping VM left running by an earlier run (PID $old_pid)..."
        kill "$old_pid" || true
        for _ in {1..30}; do
            kill -0 "$old_pid" 2>/dev/null || break
            sleep 1
        done
        kill -9 "$old_pid" 2>/dev/null || true
    fi
    rm -f vm_pid.txt
fi

# ================== SSH KEY ==================

SSH_KEY="$HOME/.ssh/id_ed25519"
if [[ ! -f "$SSH_KEY.pub" ]]; then
    log "No $SSH_KEY.pub; generating it"
    mkdir -p -m 700 "$HOME/.ssh"
    ssh-keygen -q -t ed25519 -N "" -f "$SSH_KEY"
fi
SSH_OPTS=(-o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -i "$SSH_KEY")
log "SSH key: $SSH_KEY"

# ================== PREPARE VM IMAGE ==================

if [[ -f "$BASE_IMAGE" ]]; then
    log "Found prepared image $BASE_IMAGE"
else
    # virt-customize boots the host's kernel, and Ubuntu makes it root-only
    for kernel in /boot/vmlinuz-*; do
        if [[ -e "$kernel" && ! -r "$kernel" ]]; then
            echo "ERROR: $kernel is not readable, so virt-customize can't prepare the image; run: sudo setfacl -m g:kvm:r /boot/vmlinuz-*, or 1_install_prereqs.sh" >&2
            exit 1
        fi
    done
    log "No $BASE_IMAGE; preparing it from $CLOUD_IMAGE_URL"
    mkdir -p "$IMAGES_DIR"
    # Work on a temporary name: the prepared image's existence means
    # everything is set up, so a failed preparation must not leave it behind
    wget -nv -O "$BASE_IMAGE.part" "$CLOUD_IMAGE_URL"
    qemu-img resize -q "$BASE_IMAGE.part" "$IMAGE_SIZE"
    if [[ "$ELK_VM_PASSWORD" == random ]]; then
        password_arg="$VM_USER:random"
    else
        password_arg="$VM_USER:password:$ELK_VM_PASSWORD"
    fi
    # Grow the root filesystem into the resized disk first, or --update runs
    # out of space in the cloud image's 2.2G root
    LIBGUESTFS_BACKEND=direct virt-customize -a "$BASE_IMAGE.part" \
        --run-command "{ growpart /dev/sda 1 || [ \$? -eq 1 ] || parted /dev/sda --script resizepart 1 100%; } && resize2fs /dev/sda1" \
        --network \
        --run-command "useradd -m -s /bin/bash $VM_USER" \
        --run-command "usermod -aG sudo $VM_USER" \
        --password "$password_arg" \
        --update \
        --install podman,podman-compose \
        --ssh-inject "$VM_USER:file:$SSH_KEY.pub"
    mv -f "$BASE_IMAGE.part" "$BASE_IMAGE"
    log "Prepared $BASE_IMAGE"
fi

# A new file, never written over in place: a VM still running on the old copy
# would corrupt this run's disk
rm -f "$IMAGE_NAME"
cp --reflink=auto "$BASE_IMAGE" "$IMAGE_NAME"
log "Image ready at: $IMAGE_NAME (copy of $BASE_IMAGE)"

# Without a NoCloud seed cloud-init stays off and the VM gets no network;
# a fresh instance-id makes it configure the network on every boot
SEED_DIR="$WS/vm_seed"
SEED_ISO="$WS/vm_seed.iso"
rm -rf "$SEED_DIR"
mkdir -p "$SEED_DIR"
echo "#cloud-config" > "$SEED_DIR/user-data"
printf 'instance-id: evssim-%s-%s\n' "$(date +%s)" "$$" > "$SEED_DIR/meta-data"
genisoimage -quiet -output "$SEED_ISO" -volid cidata -joliet -rock "$SEED_DIR/user-data" "$SEED_DIR/meta-data"
log "cloud-init seed ready at: $SEED_ISO"

# ================== PORT SELECTION ==================

find_free_port() {
    local port listening
    # Read the list once: with pipefail, "netstat | grep -q" fails whenever
    # grep's early exit kills netstat, and a taken port then looks free
    listening="$(netstat -tuln 2>/dev/null)"
    for port in {2224..2299}; do
        if ! grep -q ":$port " <<<"$listening"; then
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
    echo "ERROR: VM did not become ready in time (SSH not reachable with key $SSH_KEY)"
    exit 1
fi

# ================== COPY simulator & logs ==================

log "Copying simulator folder $SIMULATOR_DIR to VM..."
ssh "${SSH_OPTS[@]}" -p "$free_tcp_port" "$VM_USER@localhost" "rm -rf ~/simulator ~/logs" || true
scp "${SSH_OPTS[@]}" -P "$free_tcp_port" -r "$SIMULATOR_DIR" "$VM_USER@localhost":~/simulator

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
