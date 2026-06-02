#!/bin/bash
set -e

IMAGE_PATH="$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/$EVSSIM_QEMU_IMAGE"
TMP_IMAGE_PATH=/tmp/ubuntu-server-cloud.img

# Download official ubuntu server image and resize it
echo Downloading Guest VM image...
wget --no-verbose --show-progress -O "$TMP_IMAGE_PATH" "https://cloud-images.ubuntu.com/resolute/current/resolute-server-cloudimg-amd64.img"

echo Resizing Guest VM image...
IMG_ROOT_PART=$(virt-filesystems -la "$TMP_IMAGE_PATH" | awk '$4=="cloudimg-rootfs" {print $1}')
qemu-img create -f qcow2 "$IMAGE_PATH" "$EVSSIM_QEMU_IMAGE_SIZE"
virt-resize --expand "$IMG_ROOT_PART" "$TMP_IMAGE_PATH" "$IMAGE_PATH"
IMG_ROOT_PART=$(virt-filesystems -la "$IMAGE_PATH" | awk '$4=="cloudimg-rootfs" {print $1}')
virt-df --human-readable -a "$IMAGE_PATH"
virt-filesystems --all --long --human-readable -a "$IMAGE_PATH"
rm -f "$TMP_IMAGE_PATH"

virt-customize -a "$IMAGE_PATH" \
	--network \
	--root-password password:"$EVSSIM_QEMU_UBUNTU_ROOT_PASSWORD" \
	--run-command "groupadd --gid $EVSSIM_EXTERNAL_GID $EVSSIM_QEMU_UBUNTU_USERNAME" \
	--run-command "useradd --create-home --uid $EVSSIM_EXTERNAL_UID --gid $EVSSIM_EXTERNAL_GID --groups sudo $EVSSIM_QEMU_UBUNTU_USERNAME" \
	--run-command "echo '$EVSSIM_QEMU_UBUNTU_USERNAME ALL=(ALL) NOPASSWD: ALL' >> /etc/sudoers" \
	--password "$EVSSIM_QEMU_UBUNTU_USERNAME":password:"$EVSSIM_QEMU_UBUNTU_PASSWORD" \
	--hostname "$EVSSIM_QEMU_UBUNTU_USERNAME" \
	--run-command "echo '127.0.0.1 $EVSSIM_QEMU_UBUNTU_USERNAME' >> /etc/hosts" \
	--run-command "apt remove -y --purge cloud-init" \
	--run-command "rm -rf /etc/cloud" \
	--run-command "locale-gen en_US.UTF-8" \
	--run-command "update-locale LANG=en_US.UTF-8" \
	--run-command "echo 'LC_ALL=en_US.UTF-8' >> /etc/environment" \
	--run-command "echo 'LANGUAGE=en_US.UTF-8' >> /etc/environment" \
	--ssh-inject root:file:/scripts/id_rsa.pub \
	--ssh-inject "$EVSSIM_QEMU_UBUNTU_USERNAME":file:/scripts/id_rsa.pub \
	--update \
	--install "open-iscsi,build-essential,strace,fio,bc,libxml++2.6-2v5,libboost-filesystem1.90.0,python3-nose"

