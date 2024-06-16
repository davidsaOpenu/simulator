#!/bin/bash
source ./builder.sh

# Build kernel parts and save into the dist folder
rm -rf $EVSSIM_DIST_FOLDER/kernel
mkdir -p $EVSSIM_DIST_FOLDER/kernel

install_path=$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/kernel/
initrd_path=$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/kernel/initrd.img-$EVSSIM_KERNEL_DIST

#############################################################################################################
# TODO: get rid of the workaround 
# https://trello.com/c/8FBhuRmc/112-rch-x86-entry-thunk64o-is-not-created-unless-gcc-command-runs-explicitly
echo Workaround
evssim_run_at_folder $EVSSIM_KERNEL_FOLDER "gcc -Wp,-MD,arch/x86/entry/.thunk_64.o.d -nostdinc -isystem /usr/local/lib/gcc/x86_64-unknown-linux-gnu/4.8.4/include -I./arch/x86/include -I./arch/x86/include/generated  -I./include -I./arch/x86/include/uapi -I./arch/x86/include/generated/uapi -I./include/uapi -I./include/generated/uapi -include ./include/linux/kconfig.h -D__KERNEL__ -D__ASSEMBLY__ -fno-PIE -m64 -DCONFIG_X86_X32_ABI -DCONFIG_AS_CFI=1 -DCONFIG_AS_CFI_SIGNAL_FRAME=1 -DCONFIG_AS_CFI_SECTIONS=1 -DCONFIG_AS_FXSAVEQ=1 -DCONFIG_AS_SSSE3=1 -DCONFIG_AS_AVX=1 -DCONFIG_AS_AVX2=1 -DCONFIG_AS_AVX512=1 -DCONFIG_AS_SHA1_NI=1 -DCONFIG_AS_SHA256_NI=1 -Wa,-gdwarf-2 -DCC_USING_FENTRY   -c -o arch/x86/entry/thunk_64.o arch/x86/entry/thunk_64.S"
#############################################################################################################

evssim_run_at_folder $EVSSIM_KERNEL_FOLDER make $EVSSIM_KCONFIG -j 4
evssim_run_at_folder $EVSSIM_KERNEL_FOLDER make $EVSSIM_KCONFIG modules -j 4
evssim_run_at_folder $EVSSIM_KERNEL_FOLDER "make $EVSSIM_KCONFIG INSTALL_PATH=$install_path INSTALL_MOD_PATH=$install_path modules_install install"
EVSSIM_RUN_SUDO=y evssim_run_at_folder $EVSSIM_KERNEL_FOLDER "make $EVSSIM_KCONFIG modules_install install && mkinitramfs -o $initrd_path $EVSSIM_KERNEL_DIST"
EVSSIM_RUN_SUDO=y evssim_run_at_folder $EVSSIM_KERNEL_FOLDER chown -R external:external $initrd_path

