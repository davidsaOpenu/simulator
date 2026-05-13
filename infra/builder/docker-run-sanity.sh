#!/bin/bash
source ./builder.sh

# Make a fresh copy
evssim_qemu_fresh_image

# Run qemu
evssim_qemu_detached

# Check SSH keys:
try_ssh_key () {
	local key="$1"
	# true always returns 0
	ssh -q -i "$key" -p 2222 -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no -o PasswordAuthentication=no -o PubkeyAcceptedKeyTypes=+ssh-rsa $EVSSIM_QEMU_UBUNTU_USERNAME@localhost true
}

ssh_key_works=false
ssh_keys_path="$EVSSIM_ROOT_PATH/$EVSSIM_BUILDER_FOLDER/docker"
# Try Ed25519 Key:
if try_ssh_key $ssh_keys_path/id_ed25519; then
	ssh_key_works=true
	echo "SSH: Authentication using Ed25519 key(docker/id_ed25519) is succesfull"
else
	echo "SSH: WARNING - Authentication using Ed25519 key(docker/id_ed25519) failed"
fi
# Try RSA Key:
if try_ssh_key $ssh_keys_path/id_rsa; then
	ssh_key_works=true
	echo "SSH: Authentication using RSA key(docker/id_rsa) is succesfull"
else
	echo "SSH: WARNING - Authentication using RSA key(docker/id_rsa) failed"
fi
# Exit if neither key works:
if [ "$ssh_key_works" = "true" ]; then
	echo "SSH: Authentication using at least one key works"
else
	echo "SSH: ERROR - Failure to authenticate using any of the keys"
	exit 1
fi

# evssim_guest ls -al /dev/nvme0n1 2>/dev/null >/dev/null
# Before checking if device nvme0n1 exists we should wait
# Otherwise there are sporadic failures on "Failed to find device" error
# sleep 10

# Run a command inside the container (check if device nvme0n1 exists)
if evssim_guest ls -al /dev/nvme0n1 2>/dev/null >/dev/null; then
    echo "eVSSIM Up & Running!"
else
    echo "eVSSIM Failed to find /dev/nvme0n1."
    exit 1
fi
# Check with multiple disks
if evssim_guest ls -al /dev/nvme1n1 2>/dev/null >/dev/null; then
    echo "eVSSIM Up & Running!"
else
    echo "eVSSIM Failed to find /dev/nvme1n1."
    exit 1
fi