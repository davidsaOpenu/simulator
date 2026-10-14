#!/bin/bash
# Build everything the standalone exofs guest test needs:
#
#   kernel  - kernel 7.0.2 + exofs.ko (+ initrd) into $EXOFS_DIST_FOLDER/kernel
#   tools   - nvme-cli and mkfs.exofs, linked for the Ubuntu 14.04 guest
#   image   - Ubuntu 14.04 guest image with the 7.0.2 modules and the tools
#   qemu    - project qemu in its own worktree ($EXOFS_QEMU_FOLDER, default
#             qemu-exofs) at the fixed commit $EXOFS_QEMU_COMMIT (CR 1245496)
#
# Usage: ./exofs/build.sh [all|kernel|tools|image|qemu]...   (default: all)
#
# Nothing here writes into the shared dist/, data/, kernel/ or qemu/ trees
# and configs.json is never changed.
set -eo pipefail

EXOFS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$EXOFS_DIR/.."
# shellcheck disable=SC1091
source ./env.sh >/dev/null
# shellcheck disable=SC1091
source "$EXOFS_DIR/env-exofs.sh"
# shellcheck disable=SC1091
source ./builder.sh            # sets -eu and changes directory to EVSSIM_ROOT_PATH

ROOT="$EVSSIM_ROOT_PATH"
DROOT="$EVSSIM_DOCKER_ROOT_PATH"
DIST="$EXOFS_DIST_FOLDER"
KDIR="$EXOFS_KERNEL_FOLDER"

info() { echo "INFO [exofs-build] $*"; }
die() { echo "ERROR [exofs-build] $*" >&2; exit 1; }

require_docker_image() {
    local tag="$1"
    docker image inspect "$EVSSIM_DOCKER_IMAGE_NAME:$tag" >/dev/null 2>&1 || \
        die "docker image $EVSSIM_DOCKER_IMAGE_NAME:$tag is missing, run ./build-docker-image.sh first"
}

kernel_release() {
    [ -f "$EXOFS_KERNEL_RELEASE_FILE" ] || die "kernel is not built yet, run ./exofs/build.sh kernel"
    cat "$EXOFS_KERNEL_RELEASE_FILE"
}

# Run guestfish on the image inside the guest container
exofs_guestfish() {
    local image="$1"
    local script="$2"
    evssim_run "$EXOFS_GUEST_CONTAINER" "sudo guestfish -a '$image' -i <<'GFEOF'
$script
GFEOF"
}

###############################################################################
# kernel
###############################################################################
prepare_kernel_tree() {
    if [ "$KDIR" = "$EVSSIM_KERNEL_FOLDER" ]; then
        # explicitly pointed at the shared checkout: use it as it is
        info "using the shared kernel tree $ROOT/$KDIR as it is"
    else
        pin_worktree "$ROOT/$EVSSIM_KERNEL_FOLDER" "$ROOT/$KDIR" "$EXOFS_KERNEL_COMMIT" \
            "$EXOFS_KERNEL_GERRIT_URL" "$EXOFS_KERNEL_COMMIT_REF"
    fi
    [ -f "$ROOT/$KDIR/fs/exofs/Kconfig" ] || \
        die "$ROOT/$KDIR has no fs/exofs - it must be kernel 7.0.2 with the exofs change (CR 1245198)"
    grep -q "NVME_IOCTL_SUBMIT_OBJ_IO" "$ROOT/$KDIR/include/uapi/linux/nvme_ioctl.h" || \
        die "$ROOT/$KDIR has no NVMe object ioctl - wrong kernel tree"
}

# Sets KERNEL_BASE_CONFIG. The shared versions/kernel/7.0.2/.config may be
# missing until CR 1237773 is merged; then the file is taken from the fixed
# ref $EXOFS_KERNEL_CONFIG_REF into our dist folder (versions/ is never written).
prepare_kernel_base_config() {
    KERNEL_BASE_CONFIG="$EXOFS_KERNEL_BASE_CONFIG"
    case "$KERNEL_BASE_CONFIG" in /*) ;; *) KERNEL_BASE_CONFIG="$ROOT/$KERNEL_BASE_CONFIG" ;; esac
    [ -f "$KERNEL_BASE_CONFIG" ] && return 0

    local ref="$EXOFS_KERNEL_CONFIG_REF"
    [ -n "$ref" ] || die "kernel base config $KERNEL_BASE_CONFIG is missing"
    local repo="$ROOT/$EVSSIM_SIMULATOR_FOLDER"
    local path_in_repo="${EXOFS_KERNEL_BASE_CONFIG#$EVSSIM_SIMULATOR_FOLDER/}"
    local cached="$ROOT/$DIST/kernel-base-config-$(echo "$ref" | tr / -)"
    if [ ! -f "$cached" ]; then
        info "$KERNEL_BASE_CONFIG is missing, taking it from $ref"
        mkdir -p "$ROOT/$DIST"
        git -C "$repo" fetch -q "$EXOFS_SIMULATOR_GERRIT_URL" "$ref"
        git -C "$repo" show "FETCH_HEAD:$path_in_repo" > "$cached.tmp" || \
            die "$path_in_repo not found in $ref"
        mv "$cached.tmp" "$cached"
    fi
    KERNEL_BASE_CONFIG="$cached"
}

build_kernel() {
    require_docker_image "$EXOFS_KERNEL_COMPILE_CONTAINER"
    prepare_kernel_tree

    # Configure: shared 7.0.2 config + exofs fragment (never edits the shared config)
    if [ -f "$ROOT/$KDIR/.config" ] && [ ! -f "$ROOT/$KDIR/.config.before-exofs" ]; then
        cp "$ROOT/$KDIR/.config" "$ROOT/$KDIR/.config.before-exofs"
    fi
    prepare_kernel_base_config
    info "kernel base config: $KERNEL_BASE_CONFIG"
    cp -f "$KERNEL_BASE_CONFIG" "$ROOT/$KDIR/.config"
    chmod u+w "$ROOT/$KDIR/.config"
    evssim_run_at_folder "$EXOFS_KERNEL_COMPILE_CONTAINER" "$KDIR" \
        "./scripts/kconfig/merge_config.sh -m .config $DROOT/$EXOFS_KERNEL_CONFIG_FRAGMENT && make olddefconfig"
    grep -q '^CONFIG_EXOFS_FS=m' "$ROOT/$KDIR/.config" || die "CONFIG_EXOFS_FS=m did not survive olddefconfig"

    # Build
    info "compiling kernel in $EXOFS_KERNEL_COMPILE_CONTAINER (this takes a while the first time)"
    rm -rf "${ROOT:?}/$DIST/kernel"
    mkdir -p "$ROOT/$DIST/kernel"
    evssim_run_at_folder "$EXOFS_KERNEL_COMPILE_CONTAINER" "$KDIR" \
        "make -j$(nproc) bzImage modules && make -s kernelrelease > $DROOT/$DIST/kernel/kernel.release"
    local krel
    krel="$(kernel_release)"
    info "kernel release is $krel"

    # Install kernel image + modules into the exofs dist folder
    evssim_run_at_folder "$EXOFS_KERNEL_COMPILE_CONTAINER" "$KDIR" \
        "make INSTALL_MOD_PATH=$DROOT/$DIST/kernel INSTALL_MOD_STRIP=1 modules_install && \
         cp arch/x86/boot/bzImage $DROOT/$DIST/kernel/vmlinuz-$krel && \
         cp System.map $DROOT/$DIST/kernel/System.map-$krel && \
         cp .config $DROOT/$DIST/kernel/config-$krel"
    rm -f "$ROOT/$DIST/kernel/lib/modules/$krel/build" "$ROOT/$DIST/kernel/lib/modules/$krel/source"

    [ -f "$ROOT/$DIST/kernel/lib/modules/$krel/kernel/fs/exofs/exofs.ko" ] || die "exofs.ko was not built"
    git -C "$ROOT/$KDIR" rev-parse HEAD > "$ROOT/$DIST/kernel/kernel.commit"

    # initrd (built as root inside a throw-away container)
    EVSSIM_RUN_SUDO=y evssim_run "$EXOFS_KERNEL_COMPILE_CONTAINER" \
        "rm -rf /lib/modules/$krel && cp -a $DROOT/$DIST/kernel/lib/modules/$krel /lib/modules/ && \
         mkinitramfs -o $DROOT/$DIST/kernel/initrd.img-$krel $krel && \
         chown -R external:external $DROOT/$DIST/kernel"
    info "kernel ready: $ROOT/$DIST/kernel (vmlinuz-$krel, initrd.img-$krel, exofs.ko)"
}

###############################################################################
# tools (nvme-cli + mkfs.exofs) for the 14.04 guest
###############################################################################
build_tools() {
    require_docker_image "$EXOFS_GUEST_CONTAINER"
    [ -f "$ROOT/$KDIR/include/uapi/linux/nvme_ioctl.h" ] || die "kernel tree $ROOT/$KDIR is missing, run ./exofs/build.sh kernel"
    mkdir -p "$ROOT/$DIST"

    info "building nvme-cli ($EVSSIM_NVME_CLI_FOLDER) in $EXOFS_GUEST_CONTAINER"
    evssim_run "$EXOFS_GUEST_CONTAINER" \
        "rm -rf /tmp/nvme-cli && rsync -a --exclude=.git $DROOT/$EVSSIM_NVME_CLI_FOLDER/ /tmp/nvme-cli/ && \
         make -C /tmp/nvme-cli clean >/dev/null && make -C /tmp/nvme-cli -j$(nproc) && \
         cp /tmp/nvme-cli/nvme $DROOT/$DIST/nvme"

    # Built from a private copy so the stale objects/binary in the source
    # folder (and its uapi symlink) are never used. The sources include
    # "uapi/linux/nvme_ioctl.h", so the copy gets its own uapi link to our tree
    # (adding -I <kernel>/include instead would shadow the libc <linux/types.h>).
    info "building mkfs.exofs in $EXOFS_GUEST_CONTAINER"
    evssim_run "$EXOFS_GUEST_CONTAINER" \
        "rm -rf /tmp/mkfs.exofs && mkdir /tmp/mkfs.exofs && \
         cp $DROOT/$EVSSIM_SIMULATOR_FOLDER/infra/mkfs.exofs/*.c $DROOT/$EVSSIM_SIMULATOR_FOLDER/infra/mkfs.exofs/*.h /tmp/mkfs.exofs/ && \
         ln -s $DROOT/$KDIR/include/uapi /tmp/mkfs.exofs/uapi && \
         gcc -std=gnu99 -O2 -Wall -I/tmp/mkfs.exofs \
             -o $DROOT/$DIST/mkfs.exofs /tmp/mkfs.exofs/main.c /tmp/mkfs.exofs/exofs.c /tmp/mkfs.exofs/nvme.c"

    [ -x "$ROOT/$DIST/nvme" ] || die "nvme was not built"
    [ -x "$ROOT/$DIST/mkfs.exofs" ] || die "mkfs.exofs was not built"
    info "tools ready: $ROOT/$DIST/nvme $ROOT/$DIST/mkfs.exofs"
}

###############################################################################
# guest image (Ubuntu 14.04 + kernel 7.0.2 modules + tools)
###############################################################################
build_image() {
    require_docker_image "$EXOFS_GUEST_CONTAINER"
    local krel
    krel="$(kernel_release)"
    [ -x "$ROOT/$DIST/nvme" ] && [ -x "$ROOT/$DIST/mkfs.exofs" ] || die "tools are missing, run ./exofs/build.sh tools"

    local image="$DROOT/$DIST/$EVSSIM_QEMU_IMAGE"
    rm -f "$EXOFS_IMAGE" "$EXOFS_IMAGE_TEMPLATE"

    if [ -n "$EXOFS_BASE_IMAGE" ]; then
        local base="$EXOFS_BASE_IMAGE"
        case "$base" in /*) ;; *) base="$ROOT/$base" ;; esac
        [ -f "$base" ] || die "EXOFS_BASE_IMAGE $base does not exist"
        info "starting from existing image $base"
        cp -f "$base" "$EXOFS_IMAGE"
    else
        info "building a fresh Ubuntu 14.04 image (downloads the trusty cloud image)"
        EVSSIM_RUN_SUDO=y evssim_run "$EXOFS_GUEST_CONTAINER" "/scripts/image-maker/ubuntu-14.04.sh"
        EVSSIM_RUN_SUDO=y evssim_run "$EXOFS_GUEST_CONTAINER" "chown -R external:external $DROOT/$DIST"
    fi

    # The image must really be Ubuntu 14.04
    local release
    release="$(evssim_run "$EXOFS_GUEST_CONTAINER" "sudo virt-cat -a $image /etc/lsb-release" | tr -d '\r' | grep DISTRIB_RELEASE || true)"
    [ "$release" = "DISTRIB_RELEASE=14.04" ] || die "guest image is not Ubuntu 14.04 (got '$release')"

    info "installing kernel $krel modules, nvme and mkfs.exofs into the image"
    exofs_guestfish "$image" "copy-in $DROOT/$DIST/kernel/lib /
mkdir-p /usr/local/sbin
copy-in $DROOT/$DIST/nvme $DROOT/$DIST/mkfs.exofs /usr/local/sbin
chmod 0755 /usr/local/sbin/nvme
chmod 0755 /usr/local/sbin/mkfs.exofs
-command \"depmod -a $krel\"
mkdir-p $EVSSIM_GUEST_HOME_PATH/guest"

    cp -f "$EXOFS_IMAGE" "$EXOFS_IMAGE_TEMPLATE"
    info "image ready: $EXOFS_IMAGE_TEMPLATE"
}

###############################################################################
# qemu: own worktree of the project qemu + CR, built in the 14.04 container
###############################################################################
# Put worktree $tree of $repo exactly on the fixed $commit (no cherry-pick).
# The commit is fetched from $url $ref when the repo does not have it yet.
pin_worktree() {
    local repo="$1" tree="$2" commit="$3" url="$4" ref="$5"
    if ! git -C "$repo" cat-file -e "$commit^{commit}" 2>/dev/null; then
        info "fetching $ref from $url"
        git -C "$repo" fetch -q "$url" "$ref"
        git -C "$repo" cat-file -e "$commit^{commit}" 2>/dev/null || \
            die "$commit is not in $url $ref"
    fi
    if [ ! -d "$tree" ]; then
        info "creating worktree $tree at $commit"
        git -C "$repo" worktree prune
        git -C "$repo" worktree add --detach "$tree" "$commit"
    elif [ "$(git -C "$tree" rev-parse HEAD)" != "$(git -C "$repo" rev-parse "$commit^{commit}")" ]; then
        info "moving worktree $tree to $commit"
        git -C "$tree" checkout -q --detach "$commit" || \
            die "cannot check out $commit in $tree (local changes?)"
    else
        info "worktree $tree is at $commit"
    fi
}

# Used by all.sh: exit 0 when the built artifacts match the fixed commits.
qemu_is_current() {
    local tree="$ROOT/$EXOFS_QEMU_FOLDER"
    [ -x "$tree/x86_64-softmmu/qemu-system-x86_64" ] && \
        [ "$(cat "$tree/.exofs-qemu-commit" 2>/dev/null)" = "$EXOFS_QEMU_COMMIT" ]
}

kernel_is_current() {
    [ -f "$EXOFS_KERNEL_RELEASE_FILE" ] && \
        [ "$(cat "$ROOT/$DIST/kernel/kernel.commit" 2>/dev/null)" = "$EXOFS_KERNEL_COMMIT" ] && \
        ls "$ROOT/$DIST"/kernel/lib/modules/*/kernel/fs/exofs/exofs.ko >/dev/null 2>&1 && \
        ls "$ROOT/$DIST"/kernel/initrd.img-* >/dev/null 2>&1
}

build_qemu() {
    require_docker_image "$EXOFS_GUEST_CONTAINER"
    local repo="$ROOT/qemu"
    local tree="$ROOT/$EXOFS_QEMU_FOLDER"
    pin_worktree "$repo" "$tree" "$EXOFS_QEMU_COMMIT" "$EXOFS_QEMU_GERRIT_URL" "$EXOFS_QEMU_COMMIT_REF"
    rm -f "$tree"/.exofs-change-* "$tree/.exofs-qemu-commit"   # old cherry-pick markers
    info "updating qemu submodules"
    git -C "$tree" submodule update --init --recursive

    # runtime data link used by evssim_qemu (data folder is bind-mounted there)
    mkdir -p "$ROOT/$EVSSIM_DATA_FOLDER"
    [ -L "$tree/hw/data" ] || ln -rs "$ROOT/$EVSSIM_DATA_FOLDER" "$tree/hw/data"

    if [ ! -f "$tree/config-host.mak" ]; then
        info "configuring qemu ($EXOFS_QEMU_FOLDER) in $EXOFS_GUEST_CONTAINER"
        evssim_run_at_folder "$EXOFS_GUEST_CONTAINER" "$EXOFS_QEMU_FOLDER" ./configure \
            --enable-trace-backends=log --disable-docs --enable-tools --enable-linux-aio \
            --disable-sdl --disable-gtk --enable-kvm --target-list=x86_64-softmmu --enable-vssim \
            --disable-git-update \
            "--extra-cflags='-Wno-error=unused-but-set-variable -Wno-error=deprecated-declarations -Wno-error=cpp'"
    fi
    info "compiling qemu ($EXOFS_QEMU_FOLDER)"
    evssim_run_at_folder "$EXOFS_GUEST_CONTAINER" "$EXOFS_QEMU_FOLDER" "make -j$(nproc)"
    [ -x "$tree/x86_64-softmmu/qemu-system-x86_64" ] || die "qemu binary missing"
    echo "$EXOFS_QEMU_COMMIT" > "$tree/.exofs-qemu-commit"
    info "qemu ready: $tree/x86_64-softmmu/qemu-system-x86_64"
}

steps=("$@")
[ ${#steps[@]} -gt 0 ] || steps=(all)
for step in "${steps[@]}"; do
    case "$step" in
        all)    build_qemu; build_kernel; build_tools; build_image ;;
        kernel) build_kernel ;;
        tools)  build_tools ;;
        image)  build_image ;;
        qemu)   build_qemu ;;
        qemu-is-current)   qemu_is_current ;;
        kernel-is-current) kernel_is_current ;;
        *) die "unknown step '$step' (use all|kernel|tools|image|qemu)" ;;
    esac
done
info "done: ${steps[*]}"
