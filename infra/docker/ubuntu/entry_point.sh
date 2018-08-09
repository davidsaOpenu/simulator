#!/bin/bash
ERROR_NO_MOUNT=1
ROOT_DIR=/code
KERNEL_DIR=$ROOT_DIR/3.16.2
HDA_DIR=$ROOT_DIR/hda
NVME_CLI_DIR=$ROOT_DIR/nvme-cli
NVME_COMPL_DIR=$ROOT_DIR/nvmeCompl

SIMULATOR_DIR=$ROOT_DIR/simulator
QEMU_DIR=$SIMULATOR_DIR/eVSSIM/QEMU
DATA_DIR=$QEMU_DIR/hw/data
if [ ! -d "$ROOT_DIR" ]; then
    echo "Error: You must add the git repository as a VOLUME to the container in /code dir."
    echo "       You should add something like this to the docker run command: -v <local path of repo>:/code"
    exit $ERROR_NO_MOUNT
fi
if [ ! -d "/tmp/.X11-unix" ]; then
    echo "Error: You must mount the X11 dir for the GUI to work."
    echo "       You should add something like this to the docker run command: -v /tmp/.X11-unix:/tmp/.X11-unix"
    exit $ERROR_NO_MOUNT
fi

[ ! -d $HDA_DIR ] && echo -e "*****\nWARNING - An hda image is expects under <CODE_ROOT>/hda/<IMAGE_FILE>\n*****"
[ ! -d $KERNEL_DIR ] && echo -e "*****\nWARNING - The project '3.16.2' should be cloned and located next to simulator\n*****"
[ ! -d $NVME_CLI_DIR ] && echo -e "*****\nWARNING - The project 'nvme-cli' should be cloned and located next to simulator\n*****"
[ ! -d $NVME_COMPL_DIR ] && echo -e "*****\nWARNING - The project 'nvmeCompl' should be cloned and located next to simulator\n*****"

cd $QEMU_DIR/hw
chmod +x /code/simulator/infra/docker/ubuntu/run-ansible 
echo -e "\n\nHi you have new aliases to use:\nrun-ansible\n"
exec "$@"
