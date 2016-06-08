VSSIM talking NVMe:
==================

Building
--------

1. cd to root folder (cd .eVSSIM)
2. run scripts/compile.sh

Setting up VSSIM and run QEMU
------------------------

1. cd to root folder (cd eVSSIM)
2. run sudo scripts/run_simulator.sh <PATH-TO-IMAGE>
   the script needs root permission in order to mount a tmpfs for the simulator.

QEMU distclean
--------------

This will remove all generated VSSIM links, but will not remove the `hw/data/` directory.

```sh
cd QEMU
make distclean
```

Installing Ubuntu in QEMU image
-------------------------------

To install Ubuntu (version 14.04.1-server-amd64) into a QEMU image:

1. Follow section "Setting up VSSIM runtime".

2. Create boot image. QEMU cannot boot from NVMe.

    ```sh
    qemu-img create -f raw boot_1024M.img 1024M
    ```

3. Start Ubuntu 14.04 installation

    ```sh
    cd QEMU/hw # qemu must be run from QEMU/hw directory
    ../x86_64-softmmu/qemu-system-x86_64 -m 2048 -smp 4 \
        -hda path_to_boot_img/boot_1024M.img \
        -cdrom path_to_installation_iso/ubuntu-14.04.1-server-amd64.iso \
        -device nvme,size=4096 \
        -enable-kvm \
        -redir tcp:2222::22
    ````

4. Setup partitions:

    1. Set mount point for /dev/sda1 to be /boot

    2. Mark /dev/sda1 as bootable partition

    3. Install GRUB to /dev/sda


5. Complete the installation by rebooting the system.

Note:

1. If the machine is powered off, partions table on /dev/nvme0n1 will not be visible; see:

    * http://www.spinics.net/lists/kernel/msg1811135.html

    * http://www.spinics.net/lists/kernel/msg1759647.html

    * https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=746396

2. If CentOS is installed, the system will not boot after restart with kernel panic.
