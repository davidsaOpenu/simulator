#!/bin/bash -x
set -euo pipefail

PUBLIC_KEY=$(cat /scripts/id_rsa.pub)

# ── Required environment variables ──────────────────────────────────────────
: "${EVSSIM_EXTERNAL_GID:?}"
: "${EVSSIM_EXTERNAL_UID:?}"
: "${EVSSIM_QEMU_UBUNTU_USERNAME:?}"
: "${EVSSIM_QEMU_UBUNTU_ROOT_PASSWORD:?}"
: "${EVSSIM_QEMU_UBUNTU_PASSWORD:?}"
: "${EVSSIM_QEMU_UBUNTU_SYSTEM:?}"
: "${PUBLIC_KEY:?}"

WORK_IMG=/code/$EVSSIM_DIST_FOLDER/$EVSSIM_QEMU_IMAGE
SEED_IMG="/tmp/seed.img"

mkdir -p "$(dirname "$WORK_IMG")"

# ── 1. Download cloud image ──────────────────────────────────────────────────
if [[ "$EVSSIM_GUEST_TESTS_HOST_CONTAINER" == "ubuntu:26.04" ]]; then
    # Ubuntu 26.04: download cloud image
    BASE_IMG="/tmp/base.img"
    wget -q -O "$BASE_IMG" "$EVSSIM_GUEST_TESTS_GUEST_VM_IMAGE"
    qemu-img convert -f qcow2 -O raw "$BASE_IMG" "$WORK_IMG"
else
    # Ubuntu 14.04: download cloud image
    BASE_IMG="ubuntu-14.04-server-cloudimg-amd64-disk1.img"
    [[ -f "$BASE_IMG" ]] || wget https://cloud-images.ubuntu.com/releases/trusty/release/$BASE_IMG
    qemu-img convert -f qcow2 -O raw "$BASE_IMG" "$WORK_IMG"
fi

echo "Working image: $WORK_IMG"

# ── 2. meta-data ─────────────────────────────────────────────────────────────
cat > /tmp/meta-data << METAEOF
instance-id: evssim-$(date +%s)
local-hostname: ${EVSSIM_QEMU_UBUNTU_USERNAME}
METAEOF

# ── 3. user-data (cloud-config) ──────────────────────────────────────────────
cat > /tmp/user-data << USEREOF
#cloud-config
hostname: ${EVSSIM_QEMU_UBUNTU_USERNAME}
manage_etc_hosts: true
groups:
  - name: ${EVSSIM_QEMU_UBUNTU_USERNAME}
    gid: ${EVSSIM_EXTERNAL_GID}
users:
  - default
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
ssh_authorized_keys:
  - "${PUBLIC_KEY}"
disable_root: false
ssh_pwauth: true
locale: en_US.UTF-8
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
package_update: true
package_upgrade: false
packages:
  - python3
  - python3-nose
runcmd:
  - echo "root:${EVSSIM_QEMU_UBUNTU_ROOT_PASSWORD}" | chpasswd
  - echo "${EVSSIM_QEMU_UBUNTU_USERNAME}:${EVSSIM_QEMU_UBUNTU_PASSWORD}" | chpasswd
  - sed -i 's/^#PasswordAuthentication.*/PasswordAuthentication yes/' /etc/ssh/sshd_config
  - sed -i 's/^PasswordAuthentication no/PasswordAuthentication yes/' /etc/ssh/sshd_config
  - printf 'LABEL=cloudimg-rootfs / ext4 discard,commit=30,errors=remount-ro 0 1\nLABEL=BOOT /boot ext4 nofail 0 0\nLABEL=UEFI /boot/efi vfat nofail,x-systemd.device-timeout=5 0 0\n' > /etc/fstab
power_state:
  mode: poweroff
  timeout: 120
  condition: true
USEREOF

# ── 4. Build seed image ───────────────────────────────────────────────────────
cloud-localds "$SEED_IMG" /tmp/user-data /tmp/meta-data

# ── 5. Boot once to let cloud-init configure the image ───────────────────────
echo ">>> Booting VM for first-run configuration (this may take a few minutes)..."
qemu-system-x86_64 \
  -m 1024 \
  -smp 2 \
  -nographic \
  -drive "file=${WORK_IMG},format=raw,if=virtio" \
  -drive "file=${SEED_IMG},format=raw,if=virtio" \
  -netdev user,id=net0 \
  -device virtio-net-pci,netdev=net0 \
  -no-reboot

# ── 6. Fix image offline: SSH key, fstab, boot mounts ────────────────────────
echo ">>> Fixing image offline..."
OFFSET=$(partx -o START,TYPE -g "$WORK_IMG" 2>/dev/null | awk '/4f68bce3/{print $1; exit}')
OFFSET=$((OFFSET * 512))
mkdir -p /mnt/evssim
mount -o loop,offset=$OFFSET "$WORK_IMG" /mnt/evssim

# Add SSH authorized key
mkdir -p /mnt/evssim/home/$EVSSIM_QEMU_UBUNTU_USERNAME/.ssh
cp /scripts/id_rsa.pub /mnt/evssim/home/$EVSSIM_QEMU_UBUNTU_USERNAME/.ssh/authorized_keys
chown -R $EVSSIM_EXTERNAL_UID:$EVSSIM_EXTERNAL_GID /mnt/evssim/home/$EVSSIM_QEMU_UBUNTU_USERNAME/.ssh
chmod 700 /mnt/evssim/home/$EVSSIM_QEMU_UBUNTU_USERNAME/.ssh
chmod 600 /mnt/evssim/home/$EVSSIM_QEMU_UBUNTU_USERNAME/.ssh/authorized_keys

# Fix fstab - add nofail and disable fsck for BOOT and UEFI
printf 'LABEL=cloudimg-rootfs / ext4 discard,commit=30,errors=remount-ro 0 1\nLABEL=BOOT /boot ext4 nofail 0 0\nLABEL=UEFI /boot/efi vfat nofail,x-systemd.device-timeout=5 0 0\n' > /mnt/evssim/etc/fstab

# Mask boot and EFI mounts so they don't block boot with 7.0.2 kernel
ln -sf /dev/null /mnt/evssim/etc/systemd/system/boot.mount
ln -sf /dev/null /mnt/evssim/etc/systemd/system/boot-efi.mount

umount /mnt/evssim
echo ">>> Done. Configured image is: ${WORK_IMG}"
