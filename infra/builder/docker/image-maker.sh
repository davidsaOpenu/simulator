#!/bin/bash -x
set -euo pipefail

PUBLIC_KEY=$(cat /scripts/id_rsa.pub)
# ── Required environment variables ──────────────────────────────────────────
: "${EVSSIM_EXTERNAL_GID:?}"
: "${EVSSIM_EXTERNAL_UID:?}"
: "${EVSSIM_QEMU_UBUNTU_USERNAME:?}"
: "${EVSSIM_QEMU_UBUNTU_ROOT_PASSWORD:?}"
: "${EVSSIM_QEMU_UBUNTU_PASSWORD:?}"
: "${EVSSIM_QEMU_UBUNTU_SYSTEM:?}"   # e.g. "trusty"
: "${PUBLIC_KEY:?}"

BASE_IMG="ubuntu-14.04-server-cloudimg-amd64-disk1.img"
WORK_IMG=/code/$EVSSIM_DIST_FOLDER/$EVSSIM_QEMU_IMAGE
SEED_IMG="seed.img"


echo $WORK_IMG

[[ -f "$BASE_IMG" ]] || wget https://cloud-images.ubuntu.com/releases/trusty/release/$BASE_IMG

# ── 1. Copy base image so the original stays pristine ────────────────────────
mkdir -p "$(dirname "$WORK_IMG")"
cp "$BASE_IMG" "$WORK_IMG"

# ── 2. meta-data ─────────────────────────────────────────────────────────────
cat > meta-data <<EOF
instance-id: evssim-$(date +%s)
local-hostname: ${EVSSIM_QEMU_UBUNTU_USERNAME}
EOF

# ── 3. user-data (cloud-config) ───────────────────────────────────────────────
cat > user-data <<EOF
#cloud-config

# ── Hostname ──────────────────────────────────────────────────────────────────
hostname: ${EVSSIM_QEMU_UBUNTU_USERNAME}
manage_etc_hosts: true          # adds 127.0.0.1 <hostname> to /etc/hosts

# ── Users ─────────────────────────────────────────────────────────────────────
groups:
  - name: ${EVSSIM_QEMU_UBUNTU_USERNAME}
    gid: ${EVSSIM_EXTERNAL_GID}

users:
  - default                     # keep the cloud 'ubuntu' user if present
  - name: ${EVSSIM_QEMU_UBUNTU_USERNAME}
    uid: ${EVSSIM_EXTERNAL_UID}
    gid: ${EVSSIM_EXTERNAL_GID}
    groups: [sudo]
    sudo: "ALL=(ALL) NOPASSWD:ALL"
    shell: /bin/bash
    ssh_authorized_keys:
      - "${PUBLIC_KEY}"

chpasswd:
  list: |
    root:${EVSSIM_QEMU_UBUNTU_ROOT_PASSWORD}
    ${EVSSIM_QEMU_UBUNTU_USERNAME}:${EVSSIM_QEMU_UBUNTU_PASSWORD}
  expire: false

# ── SSH ───────────────────────────────────────────────────────────────────────
ssh_authorized_keys:
  - "${PUBLIC_KEY}"

disable_root: false             # allow root SSH login

# ── Locale ───────────────────────────────────────────────────────────────────
locale: en_US.UTF-8
locale_configfile: /etc/default/locale

# ── Files written before apt runs ─────────────────────────────────────────────
write_files:
  - path: /etc/network/interfaces.d/eth0
    content: |
      auto eth0
      iface eth0 inet dhcp

  - path: /etc/apt/sources.list
    content: |
      deb http://archive.ubuntu.com/ubuntu ${EVSSIM_QEMU_UBUNTU_SYSTEM} main universe

  - path: /etc/environment
    append: true
    content: |
      LC_ALL=en_US.UTF-8
      LANGUAGE=en_US.UTF-8

# ── Apt + packages ────────────────────────────────────────────────────────────
package_update: true
package_upgrade: false

packages:
  - libxml++2.6-2
  - libboost-filesystem1.54.0
  - python
  - python-nose

# ── Power off when done so QEMU exits ─────────────────────────────────────────
power_state:
  mode: poweroff
  timeout: 120
  condition: true
EOF

# ── 4. Build seed image ──────────────────────────────────────────────────────
cloud-localds "$SEED_IMG" user-data meta-data

# ── 5. Boot once to let cloud-init configure the image ───────────────────────
echo ">>> Booting VM for first-run configuration (this may take a few minutes)..."
qemu-system-x86_64 \
  -m 1024 \
  -smp 2 \
  -nographic \
  -drive "file=${WORK_IMG},format=qcow2,if=virtio" \
  -drive "file=${SEED_IMG},format=raw,if=virtio" \
  -netdev user,id=net0 \
  -device virtio-net-pci,netdev=net0 \
  -no-reboot

echo ">>> Done. Configured image is: ${WORK_IMG}"
