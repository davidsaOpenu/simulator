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

if [[ "$EVSSIM_GUEST_TESTS_HOST_CONTAINER" == "ubuntu:26.04" ]]; then
    # ── 1. Download Ubuntu 26.04 cloud image ────────────────────────────────
    # /tmp/base.img is intentional - temporary storage inside Docker container
    # for the downloaded cloud image before converting to raw format.
    # It gets cleaned up automatically when the container exits.
    BASE_IMG="/tmp/base.img"
    wget -q -O "$BASE_IMG" "$EVSSIM_GUEST_TESTS_GUEST_VM_IMAGE"
    qemu-img convert -f qcow2 -O raw "$BASE_IMG" "$WORK_IMG"

    echo "Working image: $WORK_IMG"

    # ── 2. meta-data ─────────────────────────────────────────────────────────
    cat > /tmp/meta-data << METAEOF
instance-id: evssim-$(date +%s)
local-hostname: ${EVSSIM_QEMU_UBUNTU_USERNAME}
METAEOF

    # ── 3. user-data for Ubuntu 26.04 ────────────────────────────────────────
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
  - path: /etc/cloud/cloud.cfg.d/99-poweroff.cfg
    content: |
      power_state:
        mode: poweroff
        timeout: 30
        condition: true
package_update: true
package_upgrade: false
packages:
  - python3
  - python3-nose
runcmd:
  - sed -i 's/^#PasswordAuthentication.*/PasswordAuthentication yes/' /etc/ssh/sshd_config
  - sed -i 's/^PasswordAuthentication no/PasswordAuthentication yes/' /etc/ssh/sshd_config
  - printf 'LABEL=cloudimg-rootfs / ext4 discard,commit=30,errors=remount-ro 0 1\nLABEL=BOOT /boot ext4 nofail 0 0\nLABEL=UEFI /boot/efi vfat nofail,x-systemd.device-timeout=5 0 0\n' > /etc/fstab
power_state:
  mode: poweroff
  timeout: 120
  condition: true
USEREOF

    # ── 4. Build seed image ───────────────────────────────────────────────────
    cloud-localds "$SEED_IMG" /tmp/user-data /tmp/meta-data

    # ── 5. Boot once - Ubuntu 26.04 uses power_state to shut down cleanly ────
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

else
    # ── 1. Download Ubuntu 14.04 cloud image ────────────────────────────────
    BASE_IMG="ubuntu-14.04-server-cloudimg-amd64-disk1.img"
    [[ -f "$BASE_IMG" ]] || wget https://cloud-images.ubuntu.com/releases/trusty/release/$BASE_IMG
    qemu-img convert -f qcow2 -O raw "$BASE_IMG" "$WORK_IMG"

    echo "Working image: $WORK_IMG"

    # ── 2. meta-data ─────────────────────────────────────────────────────────
    cat > /tmp/meta-data << METAEOF
instance-id: evssim-$(date +%s)
local-hostname: ${EVSSIM_QEMU_UBUNTU_USERNAME}
METAEOF

    # ── 3. user-data for Ubuntu 14.04 ────────────────────────────────────────
    # datasource_list prevents cloud-init from waiting for CloudStack metadata
    cat > /tmp/user-data << USEREOF
#cloud-config
datasource_list: [NoCloud, None]
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
  - path: /etc/cloud/cloud.cfg.d/99-poweroff.cfg
    content: |
      power_state:
        mode: poweroff
        timeout: 30
        condition: true
package_update: true
package_upgrade: false
packages:
  - python3
  - python3-nose
runcmd:
  - echo "datasource_list: [ NoCloud, None ]" > /etc/cloud/cloud.cfg.d/90_dpkg.cfg
  - sed -i 's/^#PasswordAuthentication.*/PasswordAuthentication yes/' /etc/ssh/sshd_config
  - sed -i 's/^PasswordAuthentication no/PasswordAuthentication yes/' /etc/ssh/sshd_config
  - printf 'LABEL=cloudimg-rootfs / ext4 discard,commit=30,errors=remount-ro 0 1\nLABEL=BOOT /boot ext4 nofail 0 0\nLABEL=UEFI /boot/efi vfat nofail,x-systemd.device-timeout=5 0 0\n' > /etc/fstab
USEREOF

    # ── 4. Build seed image ───────────────────────────────────────────────────
    cloud-localds "$SEED_IMG" /tmp/user-data /tmp/meta-data

    # ── 5. Boot once - Ubuntu 14.04 uses setsid+timeout since power_state
    #       is not reliable in cloud-init 0.7.5 ─────────────────────────────
    echo ">>> Booting VM for first-run configuration (this may take a few minutes)..."
    setsid timeout 300 qemu-system-x86_64 \
      -m 1024 \
      -smp 2 \
      -nographic \
      -drive "file=${WORK_IMG},format=raw,if=virtio" \
      -drive "file=${SEED_IMG},format=raw,if=virtio" \
      -netdev user,id=net0 \
      -device virtio-net-pci,netdev=net0 \
      -no-reboot || true
fi

echo ">>> Done. Configured image is: ${WORK_IMG}"

# ── 6. Fix image offline ──────────────────────────────────────────────────────
echo ">>> Fixing image offline..."
if [[ "$EVSSIM_GUEST_TESTS_HOST_CONTAINER" == "ubuntu:26.04" ]]; then
    OFFSET=$(partx -o START,TYPE -g "$WORK_IMG" 2>/dev/null | awk '/4f68bce3/{print $1; exit}')
else
    OFFSET=$(partx -o START,TYPE -g "$WORK_IMG" 2>/dev/null | awk 'NR==1{print $1; exit}')
fi
OFFSET=$((OFFSET * 512))
mkdir -p /mnt/evssim
mount -o loop,offset=$OFFSET "$WORK_IMG" /mnt/evssim

# Add SSH authorized key
mkdir -p /mnt/evssim/home/$EVSSIM_QEMU_UBUNTU_USERNAME/.ssh
cp /scripts/id_rsa.pub /mnt/evssim/home/$EVSSIM_QEMU_UBUNTU_USERNAME/.ssh/authorized_keys
chown -R $EVSSIM_EXTERNAL_UID:$EVSSIM_EXTERNAL_GID /mnt/evssim/home/$EVSSIM_QEMU_UBUNTU_USERNAME/.ssh
chmod 700 /mnt/evssim/home/$EVSSIM_QEMU_UBUNTU_USERNAME/.ssh
chmod 600 /mnt/evssim/home/$EVSSIM_QEMU_UBUNTU_USERNAME/.ssh/authorized_keys

# Create user and set passwords directly in the image
chroot /mnt/evssim groupadd --gid $EVSSIM_EXTERNAL_GID $EVSSIM_QEMU_UBUNTU_USERNAME 2>/dev/null || true
chroot /mnt/evssim useradd --uid $EVSSIM_EXTERNAL_UID --gid $EVSSIM_EXTERNAL_GID \
    --shell /bin/bash --create-home $EVSSIM_QEMU_UBUNTU_USERNAME 2>/dev/null || true
chroot /mnt/evssim usermod -aG sudo $EVSSIM_QEMU_UBUNTU_USERNAME 2>/dev/null || true

# Set passwords using openssl hash (works reliably for both 14.04 and 26.04)
ROOT_HASH=$(openssl passwd -1 "${EVSSIM_QEMU_UBUNTU_ROOT_PASSWORD}")
USER_HASH=$(openssl passwd -1 "${EVSSIM_QEMU_UBUNTU_PASSWORD}")
sed -i "s|^root:[^:]*:|root:${ROOT_HASH}:|" /mnt/evssim/etc/shadow
grep -q "^${EVSSIM_QEMU_UBUNTU_USERNAME}:" /mnt/evssim/etc/shadow || \
    echo "${EVSSIM_QEMU_UBUNTU_USERNAME}:${USER_HASH}:18207:0:99999:7:::" >> /mnt/evssim/etc/shadow
sed -i "s|^${EVSSIM_QEMU_UBUNTU_USERNAME}:[^:]*:|${EVSSIM_QEMU_UBUNTU_USERNAME}:${USER_HASH}:|" /mnt/evssim/etc/shadow

# Install python3-nose inside the image
cp -r /usr/lib/python3/dist-packages/nose /mnt/evssim/usr/lib/python3/dist-packages/ 2>/dev/null || true
cp /usr/bin/nosetests* /mnt/evssim/usr/bin/ 2>/dev/null || true

# Disable cloud-init on subsequent boots
touch /mnt/evssim/etc/cloud/cloud-init.disabled

# Copy configs.json, config ID and kernel version into the image
mkdir -p /mnt/evssim/etc/evssim
cp /code/$EVSSIM_BUILDER_FOLDER/versions/configs.json /mnt/evssim/etc/evssim/configs.json
echo "$EVSSIM_VERSIONS_CONFIGURATION_ID" > /mnt/evssim/etc/evssim/config_id
echo "$EVSSIM_KERNEL_DIST" > /mnt/evssim/etc/evssim/kernel_version

# Fix fstab and mask boot mounts (only needed for 26.04 GPT image)
if [[ "$EVSSIM_GUEST_TESTS_HOST_CONTAINER" == "ubuntu:26.04" ]]; then
    printf 'LABEL=cloudimg-rootfs / ext4 discard,commit=30,errors=remount-ro 0 1\nLABEL=BOOT /boot ext4 nofail 0 0\nLABEL=UEFI /boot/efi vfat nofail,x-systemd.device-timeout=5 0 0\n' > /mnt/evssim/etc/fstab
    ln -sf /dev/null /mnt/evssim/etc/systemd/system/boot.mount
    ln -sf /dev/null /mnt/evssim/etc/systemd/system/boot-efi.mount
fi

umount /mnt/evssim
echo ">>> Done. Image is ready: ${WORK_IMG}"
