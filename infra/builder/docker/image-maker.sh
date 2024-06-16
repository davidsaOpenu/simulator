#!/bin/bash
set -e

IMAGE_PATH=$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/$EVSSIM_QEMU_IMAGE
DEBOOTSTRAP_CACHE=$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/debootstrap.tgz
DEBOOTSTRAP_MIRROR=http://archive.ubuntu.com/ubuntu/
DEBOOTSTRAP_ADDITIONAL_PACKAGES=ssh
MOUNT_POINT=/mnt/guest

# Function to check the last command and exit on failure
check_command() {
    if [ $? -ne 0 ]; then
        echo "Error occurred in Stage $1"
        exit 1
    fi
}

# Stage 1: Create empty image and format it
echo "Stage 1 - Creating and formatting image"
qemu-img create -f raw $IMAGE_PATH $EVSSIM_QEMU_IMAGE_SIZE
check_command 1
mkfs.ext4 -F $IMAGE_PATH
check_command 1

# Stage 2: Mount the image on-disk. Disable delayed allocation for the initial installation
echo "Stage 2 - Mounting image and preparing mount point"
mkdir -p $MOUNT_POINT
check_command 2
mount -o loop,nodelalloc $IMAGE_PATH $MOUNT_POINT
check_command 2
rm -rf $MOUNT_POINT/*

# Stage 3: Make debootstrap/dpkg work without fsync (Feature of eatmydata)
echo "Stage 3 - Setting up debootstrap and dpkg with eatmydata"
ln -s /usr/bin/eatmydata /usr/local/bin/debootstrap
check_command 3
ln -s /usr/bin/eatmydata /usr/local/bin/dpkg
check_command 3
ln -s /usr/bin/eatmydata /usr/local/bin/wget
check_command 3

# Stage 4: Debootstrap and cache all packages
echo "Stage 4 - Debootstrap and caching packages"
if [ ! -f $DEBOOTSTRAP_CACHE ]; then
    echo "INFO: Downloading debootstrap packages"
    set +e
    debootstrap --make-tarball=$DEBOOTSTRAP_CACHE --include $DEBOOTSTRAP_ADDITIONAL_PACKAGES $EVSSIM_QEMU_UBUNTU_SYSTEM $MOUNT_POINT $DEBOOTSTRAP_MIRROR
    check_command 4
    set -e
    echo "INFO: Completed debootstrap packages $?"
fi

# Stage 5: Bootstrap into the mount point
echo "Stage 5 - Installing debootstrap packages"
debootstrap --unpack-tarball=$DEBOOTSTRAP_CACHE --include $DEBOOTSTRAP_ADDITIONAL_PACKAGES $EVSSIM_QEMU_UBUNTU_SYSTEM $MOUNT_POINT $DEBOOTSTRAP_MIRROR
check_command 5

# Stage 6: Load the content of the public key
echo "Stage 6 - Loading public key"
PUBLIC_KEY=$(cat /scripts/id_rsa.pub)
check_command 6

# Stage 7: Mount facilities
echo "Stage 7 - Mounting /proc, /sys, and /dev"
mount --bind /proc $MOUNT_POINT/proc
check_command 7
mount --bind /sys $MOUNT_POINT/sys
check_command 7
mount --bind /dev $MOUNT_POINT/dev
check_command 7

# Stage 8: Chroot and perform operations inside
echo "Stage 8 - Performing operations inside chroot"
cat << CHROOTED | chroot $MOUNT_POINT

echo "-----------------------------------"
echo "Creating user and setting passwords"
echo "-----------------------------------"
addgroup --gid $EVSSIM_EXTERNAL_GID $EVSSIM_QEMU_UBUNTU_USERNAME
adduser --gecos "" --disabled-password --uid $EVSSIM_EXTERNAL_UID --gid $EVSSIM_EXTERNAL_GID $EVSSIM_QEMU_UBUNTU_USERNAME
usermod -aG sudo $EVSSIM_QEMU_UBUNTU_USERNAME
echo "$EVSSIM_QEMU_UBUNTU_USERNAME ALL=(ALL) NOPASSWD: ALL" >> /etc/sudoers

echo "root:$EVSSIM_QEMU_UBUNTU_ROOT_PASSWORD" | chpasswd
echo "$EVSSIM_QEMU_UBUNTU_USERNAME:$EVSSIM_QEMU_UBUNTU_PASSWORD" | chpasswd

echo "----------------"
echo "Setting hostname"
echo "----------------"
echo $EVSSIM_QEMU_UBUNTU_USERNAME > /etc/hostname
echo "127.0.0.1 $EVSSIM_QEMU_UBUNTU_USERNAME" | tee -a /etc/hosts

echo "-------------------"
echo "Configuring network"
echo "--------------------"
mkdir -p /etc/netplan
cat <<EOF | tee /etc/netplan/01-netcfg.yaml
network:
  version: 2
  ethernets:
    eth0:
      dhcp4: true
EOF
chmod 600 /etc/netplan/01-netcfg.yaml
netplan apply

echo "-----------------------------"
echo "Configuring language settings"
echo "-----------------------------"
locale-gen en_US.UTF-8
update-locale LANG=en_US.UTF-8
echo "LC_ALL=en_US.UTF-8" >> /etc/environment
echo "LANGUAGE=en_US.UTF-8" >> /etc/environment

echo "---------------"
echo "Adding SSH keys"
echo "---------------"
mkdir -p /root/.ssh
echo "$PUBLIC_KEY" > /root/.ssh/authorized_keys

mkdir -p /home/$EVSSIM_QEMU_UBUNTU_USERNAME/.ssh
echo "$PUBLIC_KEY" > /home/$EVSSIM_QEMU_UBUNTU_USERNAME/.ssh/authorized_keys
chown -R $EVSSIM_QEMU_UBUNTU_USERNAME:$EVSSIM_QEMU_UBUNTU_USERNAME /home/$EVSSIM_QEMU_UBUNTU_USERNAME/.ssh

CHROOTED

check_command 8

# Stage 9: Unmount facilities
echo "Stage 9 - Unmounting /proc, /sys, and /dev"
umount $MOUNT_POINT/sys
check_command 9
umount $MOUNT_POINT/proc
check_command 9
umount $MOUNT_POINT/dev
check_command 9

# Stage 10: Unmount the image
echo "Stage 10 - Unmounting image"
umount $MOUNT_POINT
check_command 10

