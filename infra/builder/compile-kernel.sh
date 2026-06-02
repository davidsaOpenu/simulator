#!/bin/bash
source ./builder.sh

evssim_validate_version_arguments "$0" "${1:-}" "$#"
version="$1"
LOCAL_KERNEL_PATH="$EVSSIM_ROOT_PATH/$EVSSIM_KERNEL_FOLDER"

echo Switching kernel branch to $EVSSIM_KERNEL_BRANCH...
if [[ "$(git -C "$LOCAL_KERNEL_PATH" branch --show-current)" = "$EVSSIM_KERNEL_BRANCH" ]]; then
    echo Already on $EVSSIM_KERNEL_BRANCH branch. Skipping.
# Check if the repo has unsaved changes
elif [[ -n "$(git -C "$LOCAL_KERNEL_PATH" status --porcelain)" ]]; then
    echo ERROR: kernel has unsaved changes. Commit, Stash, or Reset changes and try again
    exit 1
else
    git -C "$LOCAL_KERNEL_PATH" switch "$EVSSIM_KERNEL_BRANCH"
    cp "$EVSSIM_ROOT_PATH/$EVSSIM_KERNEL_VERSIONS_FOLDER/$EVSSIM_KERNEL_BRANCH/.config" "$LOCAL_KERNEL_PATH/.config"
fi

# Build kernel parts and save into the dist folder
rm -rf $EVSSIM_DIST_FOLDER/kernel
mkdir -p $EVSSIM_DIST_FOLDER/kernel

install_path=$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/kernel/
initrd_path=$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/kernel/initrd.img-$EVSSIM_KERNEL_DIST

evssim_run_at_folder "$version" $EVSSIM_KERNEL_FOLDER "make $EVSSIM_KCONFIG -j\`nproc\`"
evssim_run_at_folder "$version" $EVSSIM_KERNEL_FOLDER "make $EVSSIM_KCONFIG modules -j\`nproc\`"
evssim_run_at_folder "$version" $EVSSIM_KERNEL_FOLDER "make $EVSSIM_KCONFIG INSTALL_PATH=$install_path INSTALL_MOD_PATH=$install_path modules_install install"
EVSSIM_RUN_SUDO=y evssim_run_at_folder "$version" $EVSSIM_KERNEL_FOLDER "make modules_install install && mkinitramfs -o $initrd_path $EVSSIM_KERNEL_DIST"
EVSSIM_RUN_SUDO=y evssim_run_at_folder "$version" $EVSSIM_KERNEL_FOLDER chown -R external:external $initrd_path
evssim_run_at_folder "$version" $EVSSIM_DIST_FOLDER "mkdir -p tools/lib && cp -r kernel/lib tools/"
