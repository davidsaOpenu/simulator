#!/bin/bash
set -eu -o pipefail

IMAGE_PATH="$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/$EVSSIM_QEMU_IMAGE"
IMAGE_NAME=resolute-server-cloudimg-amd64.img
IMAGE_BUILD=current
TMP_IMAGE_PATH=$(mktemp -d /tmp/ubuntu-cloud.img-XXXXX)

# Download official ubuntu server image and resize it
echo Downloading Guest VM image...
cd "$TMP_IMAGE_PATH"
wget --no-verbose --show-progress "https://cloud-images.ubuntu.com/resolute/$IMAGE_BUILD/$IMAGE_NAME"

echo Verifying image checksum...
wget --no-verbose --show-progress "https://cloud-images.ubuntu.com/resolute/$IMAGE_BUILD/SHA256SUMS"
wget --no-verbose --show-progress "https://cloud-images.ubuntu.com/resolute/$IMAGE_BUILD/SHA256SUMS.gpg"
gpg --keyid-format long --keyserver hkp://keyserver.ubuntu.com --recv-keys D2EB44626FDDC30B513D5BB71A5D6C4C7DB87C81
gpg --keyid-format long --verify SHA256SUMS.gpg SHA256SUMS
sha256sum --ignore-missing -c SHA256SUMS

echo Resizing Guest VM image...
IMG_ROOT_PART=$(virt-filesystems -la "$TMP_IMAGE_PATH/$IMAGE_NAME" | awk '$4=="cloudimg-rootfs" {print $1}')
qemu-img create -f qcow2 "$IMAGE_PATH" "$EVSSIM_QEMU_IMAGE_SIZE"
virt-resize --expand "$IMG_ROOT_PART" "$TMP_IMAGE_PATH/$IMAGE_NAME" "$IMAGE_PATH"
IMG_ROOT_PART=$(virt-filesystems -la "$IMAGE_PATH" | awk '$4=="cloudimg-rootfs" {print $1}')
virt-df --human-readable -a "$IMAGE_PATH"
virt-filesystems --all --long --human-readable -a "$IMAGE_PATH"
rm -rf "$TMP_IMAGE_PATH"

# Network configuration for netplan
NETWORK_YAML='
network:
  version: 2
  renderer: networkd
  ethernets:
    qemu:
      match:
        name: "enp*"
      dhcp4: true
'

virt-customize -a "$IMAGE_PATH" \
	--network \
	--root-password password:"$EVSSIM_QEMU_UBUNTU_ROOT_PASSWORD" \
	--run-command "groupadd --gid $EVSSIM_EXTERNAL_GID $EVSSIM_QEMU_UBUNTU_USERNAME" \
	--run-command "useradd --create-home --uid $EVSSIM_EXTERNAL_UID --gid $EVSSIM_EXTERNAL_GID --groups sudo --shell /bin/bash $EVSSIM_QEMU_UBUNTU_USERNAME" \
	--append-line /etc/sudoers:"$EVSSIM_QEMU_UBUNTU_USERNAME ALL=(ALL) NOPASSWD: ALL" \
	--password "$EVSSIM_QEMU_UBUNTU_USERNAME":password:"$EVSSIM_QEMU_UBUNTU_PASSWORD" \
	--hostname "$EVSSIM_QEMU_UBUNTU_USERNAME" \
	--append-line /etc/hosts:"127.0.0.1 $EVSSIM_QEMU_UBUNTU_USERNAME" \
	--write /etc/netplan/99_config.yaml:"$NETWORK_YAML" \
	--chmod 0600:/etc/netplan/99_config.yaml \
	--touch /etc/cloud/cloud-init.disabled \
	--run-command "locale-gen en_US.UTF-8" \
	--run-command "update-locale LANG=en_US.UTF-8" \
	--append-line /etc/environment:"LC_ALL=en_US.UTF-8" \
	--append-line /etc/environment:"LANGUAGE=en_US.UTF-8" \
	--ssh-inject root:file:/scripts/id_rsa.pub \
	--ssh-inject root:file:/scripts/id_ed25519.pub \
	--ssh-inject "$EVSSIM_QEMU_UBUNTU_USERNAME":file:/scripts/id_rsa.pub \
	--ssh-inject "$EVSSIM_QEMU_UBUNTU_USERNAME":file:/scripts/id_ed25519.pub \
	--run-command "ssh-keygen -A" \
	--delete /etc/ssh/sshd_config.d/60-cloudimg-settings.conf \
	--update \
	--install "open-iscsi,build-essential,strace,fio,bc,libxml++2.6-2v5,libboost-filesystem1.90.0,python3-nose"
