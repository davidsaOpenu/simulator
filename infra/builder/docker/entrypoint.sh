#!/bin/bash
set -e

# Verify all paths
mandatory_folders=("$EVSSIM_SIMULATOR_FOLDER" "$EVSSIM_KERNEL_FOLDER" "$EVSSIM_NVME_CLI_FOLDER" "$EVSSIM_NVME_COMPLIANCE_FOLDER" "$EVSSIM_HDA_FOLDER" "$EVSSIM_DATA_FOLDER" "$EVSSIM_DIST_FOLDER")
for folder in ${mandatory_folders[@]}; do
    if [ ! -d "$EVSSIM_DOCKER_ROOT_PATH/$folder" ]; then
        echo "ERROR Missing folder $folder"
        exit 1
    fi
done

# Create runtime configuration and verify virutalization
egrep -c vmx /proc/cpuinfo >/dev/null && export VIRTUALIZATION=intel
egrep -c svm /proc/cpuinfo >/dev/null && export VIRTUALIZATION=amd

# Check virtualization
if [ -z $VIRTUALIZATION ]; then
    echo "ERROR Virtualization not found"; exit 1
fi

if ! virt-host-validate >/dev/null; then
    echo "ERROR Virtualization test failed. Verify docker has permissions"; exit 1
fi

# Install the effective external user as a real user
adduser --disabled-password --gecos "" external -u $EVSSIM_EXTERNAL_UID >/dev/null
adduser external sudo >/dev/null
echo "external ALL=(ALL) NOPASSWD: ALL" >> /etc/sudoers

# Give permissions to the kvm
chmod 777 /dev/kvm

# Execute intended binary
if [ ! -z $EVSSIM_RUN_SUDO ]; then
    exec "$@"
else
    sudo -H -E -u external "$@"
fi
