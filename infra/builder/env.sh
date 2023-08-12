#!/bin/bash

if [ ! -f env.sh ]; then
    echo ERROR Please run source ./env.sh inside builder as working directory
    return 1
fi

export EVSSIM_ENVIRONMENT=yes

export EVSSIM_ROOT_PATH=$(readlink -f $(pwd)/../../..)
export EVSSIM_EXTERNAL_UID=$(id -u)
export EVSSIM_EXTERNAL_GID=$(id -g)
export EVSSIM_ENV_PATH=$(readlink -f $(pwd)/env.sh)
export EVSSIM_ENV_HASH=$(md5sum $EVSSIM_ENV_PATH | cut -d " " -f 1)

export EVSSIM_SIMULATOR_FOLDER=simulator
export EVSSIM_ELK_FOLDER=simulator/infra/elk
export EVSSIM_BUILDER_FOLDER=simulator/infra/builder
export EVSSIM_KERNEL_DIST=5.0.0+
export EVSSIM_KERNEL_FOLDER=kernel
export EVSSIM_NVME_CLI_FOLDER=nvme-cli
export EVSSIM_NVME_COMPLIANCE_FOLDER=nvmeCompl
export EVSSIM_DATA_FOLDER=data
export EVSSIM_DIST_FOLDER=dist
export EVSSIM_LOGS_FOLDER=logs

export EVSSIM_KCONFIG="CONFIG_BLK_DEV_NVME=m"

export EVSSIM_QEMU_IMAGE=system.img
export EVSSIM_QEMU_IMAGE_SIZE=20g
export EVSSIM_QEMU_UBUNTU_DEBOOTSTRAP_CACHE=deboostrap.tar
export EVSSIM_QEMU_UBUNTU_SYSTEM=trusty
export EVSSIM_QEMU_UBUNTU_USERNAME=esd
export EVSSIM_QEMU_UBUNTU_PASSWORD=esd
export EVSSIM_QEMU_UBUNTU_ROOT_PASSWORD=root
export EVSSIM_QEMU_FOLDER=qemu
export EVSSIM_QEMU_PORT=2222
export EVSSIM_QEMU_TRACE_NVME=no
export EVSSIM_QEMU_TRACE_VSSIM=no
export EVSSIM_QEMU_TRACE_BLOCK=no
export EVSSIM_QEMU_VNC=0
export EVSSIM_QEMU_SIMULATOR_ENABLED=yes
export EVSSIM_QEMU_DEFAULT_DISK_SIZE=1M

export EVSSIM_DOCKER_ROOT_PATH=/code
export EVSSIM_DOCKER_IMAGE_NAME=evssim
export EVSSIM_DOCKER_MAX_TIMEOUT_IN_MINUTES=120
export EVSSIM_DOCKER_PORTS=2222:2222,2003:2003,5900:5900
export EVSSIM_DOCKER_XOPTIONS="-v "$HOME/.Xauthority:/tmp/.Xauthority" -e DISPLAY=$DISPLAY"

export EVSSIM_GUEST_ROOT_PATH=/home/$EVSSIM_QEMU_UBUNTU_USERNAME
export EVSSIM_GUEST_MOUNT_POINT=/mnt/guest

export EVSSIM_RUNTIME_STORAGE_STRATEGY=1
export EVSSIM_RUNTIME_SSD_CONF_TEMPLATE=$EVSSIM_ROOT_PATH/$EVSSIM_BUILDER_FOLDER/docker/ssd.conf.template
export EVSSIM_RUNTIME_ALWAYS_RESET=yes

export COMPILATION_GCC=gcc-4.6
export COMPILATION_CFLAGS=-Wno-error=cpp

export ELK_ELASTICSEARCH_IMAGE="docker.elastic.co/elasticsearch/elasticsearch:8.3.2"
export ELK_KIBANA_IMAGE="docker.elastic.co/kibana/kibana:8.3.2"
export ELK_FILEBEAT_IMAGE="docker.elastic.co/beats/filebeat:8.3.2"
export ELK_TESTS_IMAGE="evssim-elk-tests"

export ELK_FILEBEAT_CONF_PATH=$EVSSIM_ROOT_PATH/$EVSSIM_ELK_FOLDER/config/filebeat-conf.yaml

export EVSSIM_CREATE_FOLDERS="$EVSSIM_DATA_FOLDER $EVSSIM_DIST_FOLDER $EVSSIM_LOGS_FOLDER"
export EVSSIM_DATA_LINKED_FOLDER="$EVSSIM_ROOT_PATH/$EVSSIM_QEMU_FOLDER/hw/data $EVSSIM_ROOT_PATH/$EVSSIM_SIMULATOR_FOLDER/eVSSIM/tests/host/data"

