#!/bin/bash
set -e

IMAGE_PATH=$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/$EVSSIM_QEMU_IMAGE
DEBOOTSTRAP_CACHE=$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/debootstrap.tgz
DEBOOTSTRAP_MIRROR=http://archive.ubuntu.com/ubuntu/
DEBOOTSTRAP_ADDITIONAL_PACKAGES=ssh
MOUNT_POINT=/mnt/guest

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

# Debootstrap and cache all packages
if [ ! -f $DEBOOTSTRAP_CACHE ]; then
    echo INFO Downloading debootstrap packages
    set +e
    debootstrap --make-tarball=$DEBOOTSTRAP_CACHE --include $DEBOOTSTRAP_ADDITIONAL_PACKAGES $EVSSIM_QEMU_UBUNTU_SYSTEM $MOUNT_POINT $DEBOOTSTRAP_MIRROR
    set -e
    echo INFO Completed debootstrap packages $?
fi

# Bootstrap into the mount point
echo INFO Installing debootstrap packages
debootstrap --unpack-tarball=$DEBOOTSTRAP_CACHE --include $DEBOOTSTRAP_ADDITIONAL_PACKAGES $EVSSIM_QEMU_UBUNTU_SYSTEM $MOUNT_POINT $DEBOOTSTRAP_MIRROR

# Debootstrap without cache currently disabled due to use of cache above
#debootstrap --include $DEBOOTSTRAP_ADDITIONAL_PACKAGES $EVSSIM_QEMU_UBUNTU_SYSTEM $MOUNT_POINT $DEBOOTSTRAP_MIRROR

# Load the content of the public key
PUBLIC_KEY=$(cat /scripts/id_rsa.pub)

# Mount facilities
mount --bind /proc $MOUNT_POINT/proc
mount --bind /sys $MOUNT_POINT/sys
mount --bind /dev $MOUNT_POINT/dev

# Chroot and do some ops inside
cat << CHROOTED | chroot $MOUNT_POINT

# Create user and change passwords
# NOTE We create the internal user with the same uid as the external use to enable editing when mounted
addgroup --gid $EVSSIM_EXTERNAL_GID $EVSSIM_QEMU_UBUNTU_USERNAME
adduser --gecos "" --disabled-password --uid $EVSSIM_EXTERNAL_UID --gid $EVSSIM_EXTERNAL_GID $EVSSIM_QEMU_UBUNTU_USERNAME
usermod -aG sudo $EVSSIM_QEMU_UBUNTU_USERNAME
echo "$EVSSIM_QEMU_UBUNTU_USERNAME ALL=(ALL) NOPASSWD: ALL" >> /etc/sudoers

echo "root:$EVSSIM_QEMU_UBUNTU_ROOT_PASSWORD" | chpasswd
echo "$EVSSIM_QEMU_UBUNTU_USERNAME:$EVSSIM_QEMU_UBUNTU_PASSWORD" | chpasswd

# Change hostname
echo $EVSSIM_QEMU_UBUNTU_USERNAME > /etc/hostname
echo "127.0.0.1 $EVSSIM_QEMU_UBUNTU_USERNAME" | tee -a /etc/hosts

# Configure network
echo auto eth0 > /etc/network/interfaces.d/eth0
echo iface eth0 inet dhcp >> /etc/network/interfaces.d/eth0

# Configure language
locale-gen en_US.UTF-8
update-locale LANG=en_US.UTF-8
echo "LC_ALL=en_US.UTF-8" >> /etc/environment
echo "LANGUAGE=en_US.UTF-8" >> /etc/environment

# Add ssh keys
mkdir -p /root/.ssh
echo "$PUBLIC_KEY" > /root/.ssh/authorized_keys

mkdir -p /home/$EVSSIM_QEMU_UBUNTU_USERNAME/.ssh
echo "$PUBLIC_KEY" > /home/$EVSSIM_QEMU_UBUNTU_USERNAME/.ssh/authorized_keys
chown -R $EVSSIM_QEMU_UBUNTU_USERNAME:$EVSSIM_QEMU_UBUNTU_USERNAME /home/$EVSSIM_QEMU_UBUNTU_USERNAME/.ssh

# Configure apt sources
echo "deb http://archive.ubuntu.com/ubuntu $EVSSIM_QEMU_UBUNTU_SYSTEM main universe" > /etc/apt/sources.list
apt update

# Additional packages
apt -y install libxml++2.6-2 libboost-filesystem1.54.0
apt -y install python python-nose python-pytest

# Services which should run immediately
# RUNLEVEL=1 apt -y install ntp ntpdate

CHROOTED

# Unmount facilities
umount $MOUNT_POINT/sys
umount $MOUNT_POINT/proc
umount $MOUNT_POINT/dev

# Unmount
umount $MOUNT_POINT
