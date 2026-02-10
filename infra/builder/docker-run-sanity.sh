#!/bin/bash
source ./builder.sh

# Make a fresh copy
evssim_qemu_fresh_image

# Run qemu
evssim_qemu_detached

# Wait for NVMe devices to appear in the guest (retry for up to 120s)
# NOTE: We use a direct SSH call with short timeouts here instead of evssim_guest,
# because evssim_guest has ConnectionAttempts=1024 which hangs for hours if the VM
# never boots (e.g. OOM crash). Each poll here takes at most ~5 seconds.
echo "Waiting for NVMe devices to appear in guest..."
SSH_POLL="ssh -q -i $EVSSIM_ROOT_PATH/$EVSSIM_BUILDER_FOLDER/docker/id_rsa -p 2222 -o ConnectTimeout=5 -o ConnectionAttempts=1 -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no -o PasswordAuthentication=no -o PubkeyAcceptedKeyTypes=+ssh-rsa $EVSSIM_QEMU_UBUNTU_USERNAME@localhost"
DEVICE_FOUND=0
for attempt in $(seq 1 24); do
    if $SSH_POLL "ls /dev/nvme0n1" 2>/dev/null >/dev/null; then
        echo "NVMe devices ready after $((attempt * 5))s"
        DEVICE_FOUND=1
        break
    fi
    echo "  Attempt $attempt/24: VM not ready yet..."
    sleep 5
done

if [ $DEVICE_FOUND -eq 0 ]; then
    echo "ERROR: Timed out waiting for VM to boot (120s). QEMU may have crashed."
    exit 1
fi

# Verify all 6 NVMe controllers are detected
echo "Verifying all NVMe controllers..."
for i in $(seq 0 5); do
    if ! evssim_guest test -e /dev/nvme${i}n1 2>/dev/null; then
        echo "ERROR: Missing /dev/nvme${i}n1"
        exit 1
    fi
done
echo "All 6 NVMe controllers detected successfully"

# Final sanity checks
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
