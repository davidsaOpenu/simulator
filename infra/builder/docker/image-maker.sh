#!/bin/bash
set -e

IMAGE_PATH=$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/$EVSSIM_QEMU_IMAGE
DEBOOTSTRAP_CACHE=$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/debootstrap.tgz
DEBOOTSTRAP_MIRROR=http://archive.ubuntu.com/ubuntu/
DEBOOTSTRAP_ADDITIONAL_PACKAGES=ssh
MOUNT_POINT=/mnt/image-maker

# Fail if file exists
if [ -e $IMAGE_PATH ]; then
    echo WARNING Image file already exists $IMAGE_PATH
    exit 1
fi

# Create empty image and format it
qemu-img create -f raw $IMAGE_PATH $EVSSIM_QEMU_IMAGE_SIZE
mkfs.ext4 -F $IMAGE_PATH

# Mount the image on-disk. Disable delayed allocation for the initial installation
mkdir -p $MOUNT_POINT
mount -o loop,nodelalloc $IMAGE_PATH $MOUNT_POINT

# Make debootstrap/dpkg work without fsync (Feature of eatmydata)
ln -s /usr/bin/eatmydata /usr/local/bin/debootstrap
ln -s /usr/bin/eatmydata /usr/local/bin/dpkg
ln -s /usr/bin/eatmydata /usr/local/bin/wget

# NOTE Current cachine of debootstrap packages is disabled due to random failures in deboostrap run
## Debootstrap and cache all packages
#if [ ! -f $DEBOOTSTRAP_CACHE ]; then
#    echo INFO Downloading debootstrap packages
#    debootstrap --make-tarball=$DEBOOTSTRAP_CACHE --include $DEBOOTSTRAP_ADDITIONAL_PACKAGES $EVSSIM_QEMU_UBUNTU_SYSTEM $MOUNT_POINT $DEBOOTSTRAP_MIRROR
#fi
## Bootstrap into the mount point
#debootstrap --unpack-tarball=$DEBOOTSTRAP_CACHE --include $DEBOOTSTRAP_ADDITIONAL_PACKAGES $EVSSIM_QEMU_UBUNTU_SYSTEM $MOUNT_POINT $DEBOOTSTRAP_MIRROR

debootstrap --include $DEBOOTSTRAP_ADDITIONAL_PACKAGES $EVSSIM_QEMU_UBUNTU_SYSTEM $MOUNT_POINT $DEBOOTSTRAP_MIRROR

# Load the content of the public key
PUBLIC_KEY=$(cat /scripts/id_rsa.pub)

# Chroot and do some ops inside
cat << CHROOTED | chroot $MOUNT_POINT

# Mount facilities
mount -t proc proc /proc
mount -t sysfs sysfs /sys

# Create user and change passwords
adduser --gecos "" --disabled-password $EVSSIM_QEMU_UBUNTU_USERNAME
usermod -aG sudo $EVSSIM_QEMU_UBUNTU_USERNAME

echo "root:$EVSSIM_QEMU_UBUNTU_ROOT_PASSWORD" | chpasswd
echo "$EVSSIM_QEMU_UBUNTU_USERNAME:$EVSSIM_QEMU_UBUNTU_PASSWORD" | chpasswd

# Change hostname
echo esd > /etc/hostname

# Configure network
echo auto eth0 > /etc/network/interfaces.d/eth0
echo iface eth0 inet dhcp >> /etc/network/interfaces.d/eth0

# Add ssh keys
mkdir -p /root/.ssh
echo "$PUBLIC_KEY" > /root/.ssh/authorized_keys

mkdir -p /home/$EVSSIM_QEMU_UBUNTU_USERNAME/.ssh
echo "$PUBLIC_KEY" > /home/$EVSSIM_QEMU_UBUNTU_USERNAME/.ssh/authorized_keys
chown -R $EVSSIM_QEMU_UBUNTU_USERNAME:$EVSSIM_QEMU_UBUNTU_USERNAME /home/$EVSSIM_QEMU_UBUNTU_USERNAME/.ssh

# Unmount facilities
umount /sys
umount /proc

CHROOTED

# Unmount
umount $MOUNT_POINT