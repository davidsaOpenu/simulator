#!/usr/bin/env bash
# Prerequisites of the simulator's CI on the machine that builds it:
# - infra/builder/run-ci.sh: docker (build images, run the builder), docker
#   compose (the ELK stack), ansible, tox;
# - infra/ELK/test_on_podman_VM.sh: a QEMU/KVM guest, reached over ssh/scp
#   on a port netstat finds free, booted from an image it downloads (wget) and
#   prepares (qemu-img, virt-customize) with a cloud-init seed (genisoimage).
# Jenkins' disaster recovery also relies on it for xv6's job (same QEMU,
# netstat and ssh needs) — xv6 has no script of its own.
#
#   sudo ./1_install_prereqs.sh
#
# Adds the invoking user (SUDO_USER) to the docker and kvm groups — log
# out and back in for it to take effect — and gives it no sudo rights.
# Jenkins' disaster recovery runs it as root with SUDO_USER=jenkins: that
# only names the account. Safe to re-run.
#
# On Ubuntu, kernel images are root-only but virt-customize (which
# test_on_podman_VM.sh runs unprivileged to prepare its image) has to read
# them: lets the kvm group read them, now and after each kernel update.
set -euo pipefail

if [ "$(id -u)" -ne 0 ]; then
  echo "Please run this script as root or using sudo!"
  exit 1
fi

OS_ID=""
VERSION_ID=""

if [ -f /etc/os-release ]; then
    . /etc/os-release
    OS_ID=${ID}
    VERSION_ID=${VERSION_ID:-}
elif [ -f /etc/lsb-release ]; then
    . /etc/lsb-release
    OS_ID=${DISTRIB_ID}
    VERSION_ID=${DISTRIB_RELEASE:-}
fi

if [ -z "$OS_ID" ]; then
    echo "Unsupported OS: Cannot determine distribution."
    exit 1
fi

# normalize OS_ID to lowercase for case-insensitive matching
OS_ID=$(echo "$OS_ID" | tr '[:upper:]' '[:lower:]')

check_success() {
    if [ $? -ne 0 ]; then
        echo "Error: $1 failed."
        exit 1
    fi
}

USERNAME="${SUDO_USER:-root}"

post_install_common() {
    systemctl enable --now docker
    check_success "!!"

    usermod -a -G docker,kvm "$USERNAME"
    check_success "!!"

    # The scripts call qemu-system-x86_64 — RHEL-family qemu-kvm ships only
    # /usr/libexec/qemu-kvm
    if ! command -v qemu-system-x86_64 &>/dev/null && [ -x /usr/libexec/qemu-kvm ]; then
        ln -sf /usr/libexec/qemu-kvm /usr/local/bin/qemu-system-x86_64
        check_success "!!"
    fi

    # Fail now, not in the middle of a CI run
    local tool
    for tool in docker ansible tox qemu-system-x86_64 netstat ssh scp \
                wget qemu-img virt-customize genisoimage; do
        command -v "$tool" &>/dev/null || { echo "Error: $tool is still missing."; exit 1; }
    done
    docker compose version
    check_success "docker compose"
}

install_for_fedora() {
    dnf install -y moby-engine \
                   docker-compose \
                   ansible \
                   python3-tox \
                   qemu-system-x86-core \
                   net-tools \
                   openssh-clients \
                   wget \
                   qemu-img \
                   guestfs-tools \
                   genisoimage
    check_success "!!"
}

install_for_centos() {
    dnf install -y dnf-plugins-core epel-release
    check_success "!!"

    # Docker isn't packaged for the RHEL family
    dnf config-manager --add-repo https://download.docker.com/linux/centos/docker-ce.repo
    check_success "!!"

    dnf install -y docker-ce \
                   docker-ce-cli \
                   containerd.io \
                   docker-buildx-plugin \
                   docker-compose-plugin \
                   ansible \
                   tox \
                   qemu-kvm \
                   net-tools \
                   openssh-clients \
                   wget \
                   qemu-img \
                   guestfs-tools \
                   genisoimage
    check_success "!!"
}

install_for_ubuntu() {
    apt-get update
    check_success "!!"
    # On 22.04 tox is packaged as "tox", not "python3-tox"
    apt-get install -y \
        docker.io \
        docker-compose-v2 \
        ansible \
        tox \
        qemu-system-x86 \
        net-tools \
        openssh-client \
        acl \
        wget \
        qemu-utils \
        libguestfs-tools \
        genisoimage
    check_success "!!"
}

allow_kvm_group_to_read_kernels() {
    local hook=/etc/kernel/postinst.d/zz-vmlinuz-kvm-read kernel
    # chmod before setfacl: chmod on a file with an ACL rewrites its mask
    for kernel in /boot/vmlinuz-*; do
        [ -e "$kernel" ] || continue
        chmod 0600 "$kernel"
        check_success "!!"
        setfacl -m g:kvm:r "$kernel"
        check_success "!!"
    done
    # Ubuntu installs each new kernel as 0600. A failing hook would fail the
    # kernel's package installation, so it only warns.
    cat > "$hook" <<'EOF'
#!/bin/sh
setfacl -m g:kvm:r "/boot/vmlinuz-$1" ||
    echo "$0: kvm group can't read /boot/vmlinuz-$1; virt-customize will fail" >&2
exit 0
EOF
    check_success "!!"
    chmod 755 "$hook"
    check_success "!!"
}

case "$OS_ID" in
  fedora)
    if [[ "$VERSION_ID" != "42" ]]; then
      echo "Only Fedora 42 is supported. Detected: Fedora $VERSION_ID"
      exit 1
    fi
    install_for_fedora
    ;;
  centos|rhel|rocky|almalinux)
    if [[ "${VERSION_ID%%.*}" != "9" ]]; then
      echo "Only version 9 of CentOS Stream / RHEL / Rocky / AlmaLinux is supported. Detected: $OS_ID $VERSION_ID"
      exit 1
    fi
    install_for_centos
    ;;
  ubuntu)
    if [[ "$VERSION_ID" != "22.04" ]]; then
      echo "Only Ubuntu 22.04 is supported. Detected: Ubuntu $VERSION_ID"
      exit 1
    fi
    install_for_ubuntu
    allow_kvm_group_to_read_kernels
    ;;
  *)
    echo "Unsupported OS: $OS_ID $VERSION_ID. Only Fedora 42, CentOS Stream / RHEL / Rocky / AlmaLinux 9 and Ubuntu 22.04 are supported."
    exit 1
    ;;
esac

post_install_common

echo "System preparation for $OS_ID $VERSION_ID completed successfully."
