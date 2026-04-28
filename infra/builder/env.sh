#!/bin/bash

if [ ! -f env.sh ]; then
    echo ERROR Please run source ./env.sh inside builder as working directory
    return 1
fi


# ============================================================
# Load configuration from JSON
# ============================================================
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Load config from JSON using jq
load_config() {
    local config_id=$1
    local config_file="${SCRIPT_DIR}/configs.json"

    if ! command -v jq &> /dev/null; then
        echo "WARN: jq not found. Skipping JSON config loading."
        echo "      Install with: apt install jq"
        return 1
    fi

    local cfg=".configs[] | select(.id == $config_id)"

    if [[ ! -f "$config_file" ]]; then
        echo "ERROR: Config ID $config_id not found in $config_file"
        return 1
    fi
    export EVSSIM_KERNEL_VERSION=$(jq -r "$cfg | .kernel" "$config_file")
    export EVSSIM_QEMU_VERSION=$(jq -r "$cfg | .qemu" "$config_file")
    export EVSSIM_GUEST_VM_FLAVOR=$(jq -r "$cfg | .guestVM" "$config_file")
    export EVSSIM_CONTAINER_IMG=$(jq -r "$cfg | .containerImg" "$config_file")

    echo "EVSSIM_KERNEL_VERSION=$EVSSIM_KERNEL_VERSION"
    echo "EVSSIM_QEMU_VERSION=$EVSSIM_QEMU_VERSION"
    echo "EVSSIM_GUEST_VM_FLAVOR=$EVSSIM_GUEST_VM_FLAVOR"
    echo "EVSSIM_CONTAINER_IMG=$EVSSIM_CONTAINER_IMG"
}

# Load default config (config 2)
load_config 2


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
export EVSSIM_DOCKER_MAX_TIMEOUT_IN_MINUTES=240
export EVSSIM_DOCKER_PORTS_OPTION="-p 2222:2222 -p 2003:2003 -p 5900:5900"
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
