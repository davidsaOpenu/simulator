#!/bin/bash
set -eu -o pipefail

IMAGE_PATH="$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/$EVSSIM_QEMU_IMAGE"
IMAGE_NAME=trusty-server-cloudimg-amd64-disk1.img
IMAGE_BUILD=current
TMP_IMAGE_PATH=$(mktemp -d /tmp/ubuntu-cloud.img-XXXXX)

# Load the content of the public key
PUBLIC_KEY_RSA=$(cat /scripts/id_rsa.pub)
PUBLIC_KEY_ED25519=$(cat /scripts/id_ed25519.pub)

# Download official ubuntu server image and resize it
echo Downloading Guest VM image...
cd "$TMP_IMAGE_PATH"
wget --no-verbose "https://cloud-images.ubuntu.com/trusty/$IMAGE_BUILD/$IMAGE_NAME"

echo Verifying image checksum...
wget --no-verbose "https://cloud-images.ubuntu.com/trusty/$IMAGE_BUILD/SHA256SUMS"
wget --no-verbose "https://cloud-images.ubuntu.com/trusty/$IMAGE_BUILD/SHA256SUMS.gpg"
gpg --keyid-format long --keyserver hkp://keyserver.ubuntu.com --recv-keys D2EB44626FDDC30B513D5BB71A5D6C4C7DB87C81
gpg --keyid-format long --verify SHA256SUMS.gpg SHA256SUMS
 # Delete all checksums except for the downloaded image checksum
grep "$IMAGE_NAME" SHA256SUMS > sums && mv sums SHA256SUMS
sha256sum -c SHA256SUMS

echo Resizing Guest VM image...
IMG_ROOT_PART=$(virt-filesystems -la "$TMP_IMAGE_PATH/$IMAGE_NAME" | awk '$4=="cloudimg-rootfs" {print $1}')
qemu-img create -f qcow2 "$IMAGE_PATH" "$EVSSIM_QEMU_IMAGE_SIZE"
virt-resize --expand "$IMG_ROOT_PART" "$TMP_IMAGE_PATH/$IMAGE_NAME" "$IMAGE_PATH"
IMG_ROOT_PART=$(virt-filesystems -la "$IMAGE_PATH" | awk '$4=="cloudimg-rootfs" {print $1}')
virt-df --human-readable -a "$IMAGE_PATH"
virt-filesystems --all --long --human-readable -a "$IMAGE_PATH"
rm -rf "$TMP_IMAGE_PATH"

guestfish --network -a "$IMAGE_PATH" -i << EOF
# Create user and change passwords
# NOTE We create the internal user with the same uid as the external use to enable editing when mounted
command "groupadd --gid $EVSSIM_EXTERNAL_GID $EVSSIM_QEMU_UBUNTU_USERNAME"
command "useradd --create-home --uid $EVSSIM_EXTERNAL_UID --gid $EVSSIM_EXTERNAL_GID --groups sudo $EVSSIM_QEMU_UBUNTU_USERNAME"
write-append /etc/sudoers "$EVSSIM_QEMU_UBUNTU_USERNAME ALL=(ALL) NOPASSWD: ALL\n"
sh "echo 'root:$EVSSIM_QEMU_UBUNTU_ROOT_PASSWORD' | chpasswd"
sh "echo '$EVSSIM_QEMU_UBUNTU_USERNAME:$EVSSIM_QEMU_UBUNTU_PASSWORD' | chpasswd"

# Change hostname
write /etc/hostname "$EVSSIM_QEMU_UBUNTU_USERNAME\n"
sh "echo '127.0.0.1 $EVSSIM_QEMU_UBUNTU_USERNAME' | tee -a /etc/hosts"

# Disable cloud-init
command "apt remove -y --purge cloud-init"
command "rm -rf /etc/cloud"

# Configure language
sh "locale-gen en_US.UTF-8"
sh "update-locale LANG=en_US.UTF-8"
write-append /etc/environment "LC_ALL=en_US.UTF-8\n"
write-append /etc/environment "LANGUAGE=en_US.UTF-8\n"

# Add ssh keys
command "mkdir -p /root/.ssh"
command "mkdir -p /home/$EVSSIM_QEMU_UBUNTU_USERNAME/.ssh"
write /root/.ssh/authorized_keys "$PUBLIC_KEY_RSA\n$PUBLIC_KEY_ED25519\n"
write /home/$EVSSIM_QEMU_UBUNTU_USERNAME/.ssh/authorized_keys "$PUBLIC_KEY_RSA\n$PUBLIC_KEY_ED25519\n"
command "chown -R $EVSSIM_QEMU_UBUNTU_USERNAME:$EVSSIM_QEMU_UBUNTU_USERNAME /home/$EVSSIM_QEMU_UBUNTU_USERNAME/.ssh"
command "ssh-keygen -A"
write-append /etc/ssh/sshd_config "PasswordAuthentication yes\n"

# Additional packages
command "apt update"
command "apt -y install open-iscsi open-iscsi-utils build-essential strace fio bc libxml++2.6-2 libboost-filesystem1.54.0 python python-nose"

EOF
