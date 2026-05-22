#!/bin/bash

if [ ! -f env.sh ]; then
    echo ERROR Please run source ./env.sh inside builder as working directory
    return 1
fi


######################################################################################################
#                  eVSSIM version confuguration variables
######################################################################################################
export EVSSIM_VERSIONS_CONFIGURATION="versions"
export EVSSIM_VERSIONS_CONFIGURATION_ID=2
# Export:
#   EVSSIM_KERNEL_BRANCH
#   EVSSIM_KERNEL_COMPILE_CONTAINER
#   EVSSIM_QEMU_BRANCH
#   EVSSIM_QEMU_COMPILE_CONTAINER
#   EVSSIM_NVME_CLI_BRANCH
#   EVSSIM_NVME_CLI_COMPILE_CONTAINER
#   EVSSIM_HOST_TESTS_COMPILE_CONTAINER
#   EVSSIM_HOST_TESTS_RUN_CONTAINER
#   EVSSIM_GUEST_TESTS_COMPILE_CONTAINER
#   EVSSIM_GUEST_TESTS_GUEST_VM_IMAGE
source load_config.sh

########################################################################################################
#                  end of eVSSIM version confuguration variables
########################################################################################################

export EVSSIM_ENVIRONMENT=yes

export EVSSIM_ROOT_PATH=$(readlink -f $(pwd)/../../..)
export EVSSIM_EXTERNAL_UID=$(id -u)
export EVSSIM_EXTERNAL_GID=$(id -g)
export EVSSIM_ENV_PATH=$(readlink -f $(pwd)/env.sh)
export EVSSIM_ENV_HASH=$(md5sum $EVSSIM_ENV_PATH | cut -d " " -f 1)

export EVSSIM_SIMULATOR_FOLDER=simulator
export EVSSIM_ELK_FOLDER=simulator/infra/elk
export EVSSIM_BUILDER_FOLDER=simulator/infra/builder
export EVSSIM_VERSIONS_FOLDER=$EVSSIM_BUILDER_FOLDER/versions
export EVSSIM_CONTAINER_VERSIONS_FOLDER=$EVSSIM_VERSIONS_FOLDER/containers
export EVSSIM_IMG_MAKER_VERSIONS_FOLDER=$EVSSIM_VERSIONS_FOLDER/image-maker
export EVSSIM_KERNEL_FOLDER=kernel
export EVSSIM_KERNEL_MAKE_ARGS="CONFIG_BLK_DEV_NVME=m"
export EVSSIM_KERNEL_VERSION=$(git -C $EVSSIM_ROOT_PATH/$EVSSIM_KERNEL_FOLDER show $EVSSIM_KERNEL_BRANCH:Makefile | awk '/^VERSION/ {v=$3} /^PATCHLEVEL/ {p=$3} /^SUBLEVEL/ {s=$3} END {printf "%s.%s.%s\n", v, p, s}')
export EVSSIM_KERNEL_DIST="$EVSSIM_KERNEL_VERSION+"
export EVSSIM_KERNEL_VERSIONS_FOLDER=$EVSSIM_VERSIONS_FOLDER/kernel
export EVSSIM_NVME_CLI_FOLDER=nvme-cli
export EVSSIM_NVME_COMPLIANCE_FOLDER=nvmeCompl
export EVSSIM_DATA_FOLDER=data
export EVSSIM_DIST_FOLDER=dist
export EVSSIM_LOGS_FOLDER=logs
export EVSSIM_SSH_KEYS_PATH="$EVSSIM_ROOT_PATH/$EVSSIM_BUILDER_FOLDER/docker"

export EVSSIM_QEMU_IMAGE=system.img
export EVSSIM_QEMU_IMAGE_SIZE=20g
export EVSSIM_QEMU_UBUNTU_USERNAME=esd
export EVSSIM_QEMU_UBUNTU_PASSWORD=esd
export EVSSIM_QEMU_UBUNTU_ROOT_PASSWORD=root
export EVSSIM_QEMU_FOLDER=qemu
export EVSSIM_QEMU_SSH_PORT=2222
export EVSSIM_QEMU_TRACE_NVME=no
export EVSSIM_QEMU_TRACE_VSSIM=no
export EVSSIM_QEMU_TRACE_BLOCK=no
export EVSSIM_QEMU_VNC=0
export EVSSIM_QEMU_SIMULATOR_ENABLED=yes
export EVSSIM_QEMU_DEFAULT_DISK_SIZE=1M

export EVSSIM_DOCKER_ROOT_PATH=/code
export EVSSIM_DOCKER_IMAGE_NAME=evssim
export EVSSIM_DOCKER_MAX_TIMEOUT_IN_MINUTES=240
export EVSSIM_DOCKER_PORTS_OPTION="-p $EVSSIM_QEMU_SSH_PORT:$EVSSIM_QEMU_SSH_PORT -p 2003:2003 -p 5900:5900"
export EVSSIM_DOCKER_XOPTIONS="-v "$HOME/.Xauthority:/tmp/.Xauthority" -e DISPLAY=$DISPLAY"

export EVSSIM_GUEST_ROOT_PATH=/home/$EVSSIM_QEMU_UBUNTU_USERNAME

export EVSSIM_RUNTIME_STORAGE_STRATEGY=1
export EVSSIM_RUNTIME_SSD_CONF_TEMPLATE=$EVSSIM_ROOT_PATH/$EVSSIM_BUILDER_FOLDER/docker/ssd.conf.template
export EVSSIM_RUNTIME_ALWAYS_RESET=yes

export ELK_ELASTICSEARCH_IMAGE="docker.elastic.co/elasticsearch/elasticsearch:8.3.2"
export ELK_KIBANA_IMAGE="docker.elastic.co/kibana/kibana:8.3.2"
export ELK_FILEBEAT_IMAGE="docker.elastic.co/beats/filebeat:8.3.2"
export ELK_TESTS_IMAGE="evssim-elk-tests"

export ELK_FILEBEAT_CONF_PATH=$EVSSIM_ROOT_PATH/$EVSSIM_ELK_FOLDER/config/filebeat-conf.yaml

export EVSSIM_CREATE_FOLDERS="$EVSSIM_DATA_FOLDER $EVSSIM_DIST_FOLDER $EVSSIM_LOGS_FOLDER"
export EVSSIM_DATA_LINKED_FOLDER="$EVSSIM_ROOT_PATH/$EVSSIM_QEMU_FOLDER/hw/data $EVSSIM_ROOT_PATH/$EVSSIM_SIMULATOR_FOLDER/eVSSIM/tests/host/data"
