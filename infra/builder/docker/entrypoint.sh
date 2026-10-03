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

# Check virtualization.
# LOCAL DEV PATCH (uncommitted): restores the EVSSIM_ALLOW_NO_KVM escape hatch
# from the pending local commit 3d130fe, without which nothing runs on a host
# lacking /dev/kvm (WSL). Not committed here so it does not conflict with that
# commit when it is rebased onto the multi-container infrastructure.
if [ -z ${VIRTUALIZATION:-} ]; then
    if [ -n "${EVSSIM_ALLOW_NO_KVM:-}" ]; then
        echo "WARNING Virtualization unavailable; QEMU commands will use TCG fallback."
    else
        echo "ERROR Virtualization not found"; exit 1
    fi
fi

if ! virt-host-validate qemu > /dev/null; then # verify only qemu, lxc is irrelevant
    if [ -n "${EVSSIM_ALLOW_NO_KVM:-}" ]; then
        echo "WARNING Virtualization validation failed; QEMU commands will use TCG fallback."
    else
        echo "ERROR Virtualization test failed. Run virt-host-validate \
              from the CLI and fix any reported issues."; exit 1
    fi
fi

# Install the effective external user as a real user
addgroup --gid $EVSSIM_EXTERNAL_GID external >/dev/null
adduser --disabled-password --gecos "" --uid $EVSSIM_EXTERNAL_UID --gid $EVSSIM_EXTERNAL_GID external >/dev/null
adduser external sudo >/dev/null
echo "external ALL=(ALL) NOPASSWD: ALL" >> /etc/sudoers

# Map X authentication if any available
if [ -f /tmp/.Xauthority ]; then
    ln -s /tmp/.Xauthority /home/external/.Xauthority
    chown external:external /home/external/.Xauthority
fi

# Give permissions to the kvm (LOCAL DEV PATCH: absent on hosts without KVM)
[ -e /dev/kvm ] && chmod 777 /dev/kvm

# Execute intended binary
if [ ! -z ${EVSSIM_RUN_SUDO:-} ]; then
    exec "$@"
else
    exec sudo -H -E -u external "$@"
fi
