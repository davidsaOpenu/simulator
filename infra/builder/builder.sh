#!/bin/bash

set -eu -o pipefail

declare -x EVSSIM_DOCKER_UUID

# Verify environment is loaded
if [ -z ${EVSSIM_ENVIRONMENT:-} ]; then
    echo "ERROR Builder not running in evssim environment. Please execute 'source ./env.sh' first"
    exit 1
fi

# Running as root is not supported
if [ "$UID" -eq "0" ]; then
    echo "ERROR Builder does not support execution under root"
    exit 1
fi

# Warn if dist is not tmpfs
if [[ $(df --output=fstype "$EVSSIM_ROOT_PATH/$EVSSIM_DIST_FOLDER" 2>/dev/null | grep tmpfs) != tmpfs ]]; then
    echo "WARNING dist folder is not tmpfs mapped. Consider executing"
    echo "        $ sudo mount -t tmpfs -o size=4g tmpfs \"$EVSSIM_ROOT_PATH/$EVSSIM_DIST_FOLDER\""
fi

# Warn if environment changed
if [ -z ${EVSSIM_ENV_PATH:-} ]; then
    echo "ERROR Missing environment file path"
    exit 1
elif [[ $(md5sum $EVSSIM_ENV_PATH | cut -d " " -f 1) != $EVSSIM_ENV_HASH ]]; then
    echo "WARNING Environment file hash changed. Please reload using 'source ./env.sh'"
fi

# Check for docker support
if ! which docker >/dev/null; then
    echo "ERROR Missing docker configuration"
    exit 1
fi

if ! docker ps 2>/dev/null >/dev/null; then
    echo "ERROR Docker has no permissions. Consider adding user to docker group. Logout and login afterwards."
    echo "      $ sudo groupadd docker"
    echo "      $ sudo usermod -aG docker $USER"
    exit 1
fi

# Configure docker use of tty if one is available
docker_extra_tty=""
if [ -t 0 ]; then
    docker_extra_tty=-t
fi

# Change to project root
cd "$EVSSIM_ROOT_PATH"

# Create output folders (Might already exist)
for folder in $EVSSIM_CREATE_FOLDERS; do
    if [ ! -d "$folder" ]; then mkdir "$folder"; fi
done

# Correct permissions issues
chmod 600 "$EVSSIM_SSH_KEYS_PATH/id_rsa"
chmod 600 "$EVSSIM_SSH_KEYS_PATH/id_ed25519"

# Link dist folder
for folder in $EVSSIM_DATA_LINKED_FOLDER; do
    if [ ! -L $folder ]; then
        ln -rs $EVSSIM_ROOT_PATH/$EVSSIM_DATA_FOLDER $folder
    fi
done

# Values used in multiple functions:
IMAGE_PATH="$EVSSIM_ROOT_PATH/$EVSSIM_DIST_FOLDER/$EVSSIM_QEMU_IMAGE"
IMAGE_PATH_TEMPLATE="$EVSSIM_ROOT_PATH/$EVSSIM_DIST_FOLDER/$EVSSIM_QEMU_IMAGE.template"
INTERNAL_IMAGE_PATH="$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/$EVSSIM_QEMU_IMAGE"
MOUNT_POINT=/mnt/guest

# Run new instance of docker in a specific root folder
# Parameters
#  - tag - Docker Image ":Tag" to run under.
#  - folder - Folder name under the project root
#  - command - Command to execute
evssim_run_at_folder () {
    evssim_run_at_path "$1" "$EVSSIM_DOCKER_ROOT_PATH/$2" "${@:3}"
}

# Run new instance of docker in a specific path
# Parameters
#  - tag - Docker Image ":Tag" to run under.
#  - path - Path inside docker
#  - command - Command to execute
evssim_run_at_path () {
    local tag="$1"
    local path="$2"
    local args="${@:3}"
    docker run --rm -i $docker_extra_tty $EVSSIM_DOCKER_XOPTIONS $EVSSIM_DOCKER_PORTS_OPTION --privileged --env-file <(evssim_all_env) -v $EVSSIM_ROOT_PATH:$EVSSIM_DOCKER_ROOT_PATH "$EVSSIM_DOCKER_IMAGE_NAME:$tag" bash -c "cd $path; $args"
}

# Run
# Parameters
#  - tag - Docker Image ":Tag" to run under.
evssim_run () {
    local tag="$1"
    shift # Remove the tag parameter from "$@"
    local com="$@"
    evssim_run_at_path "$tag" "$EVSSIM_DOCKER_ROOT_PATH" "$com"
}

# Build SSD configuration from environment
# Parameters - None
evssim_build_ssd_conf () {
    python -c "import os; import sys; sys.stdout.write(open('$EVSSIM_RUNTIME_SSD_CONF_TEMPLATE', 'rt').read() % os.environ)"
}

# Calculate ssd disk sizes from ssd configuration (returns array of sizes)
# Parameters - None
evssim_calculate_ssd_conf_disk_sizes() {
    local code=$(cat <<PYTHON
# Parse ssd configuration - all devices
import sys

lines = [line.strip() for line in sys.stdin.readlines()]
devices = {}
current_device = None

for line in lines:
    if not line:
        continue

    # Check if this is a device header like [nvme01]
    if line.startswith('[') and line.endswith(']'):
        current_device = line[1:-1]  # Remove brackets
        if current_device not in devices:
            devices[current_device] = {}
        continue

    # Parse key-value pairs
    parts = line.split(' ', 1)
    if len(parts) == 2:
        key, value = parts
        devices[current_device][key] = value

# Function to get value from specific device
def g(device_data, name):
    return int(device_data.get(name, 0))

# Calculate sizes for all devices
for device_name in sorted(devices.keys()):
    device_data = devices[device_name]
    size = g(device_data, "FLASH_NB") * g(device_data, "BLOCK_NB") * g(device_data, "PAGE_NB") * g(device_data, "PAGE_SIZE")
    size -= g(device_data, "PAGE_NB") * g(device_data, "PAGE_SIZE") # GC reserved pages
    print(size)
PYTHON
)
    echo "$ssd" | python -c "$code"
}

# Get device count from ssd configuration
# Parameters - None
evssim_get_device_count() {
    local code=$(cat <<PYTHON
import sys
count = 0
for line in sys.stdin.readlines():
    line = line.strip()
    if line.startswith('[') and line.endswith(']'):
        count += 1
print(count)
PYTHON
)
    echo "$ssd" | python -c "$code"
}

# Internal Helper Function
# Outputs a "Usage: " message to stdout.
# Parameters:
#  - progname - The program's name.
#  - summary - An optional summary line to print under "Usage: "
_evssim_arguments_usage() {
    local progname="$1"
    local summary="$2"
    local valid_versions="$(docker image ls "$EVSSIM_DOCKER_IMAGE_NAME" --format '{{.Tag}}')"

    echo Usage: $progname CONTAINER_VERSION
    if [ ! -z "$summary" ]; then
        echo $summary
    fi
    echo Valid versions: $valid_versions
}

# Internal Helper Function
# Outputs an error followed by a "Usage: " message to stdout.
# Parameters:
#  - progname - The program's name.
#  - error - An error message to output.
#  - summary - An optional summary line to print under "Usage: "
_evssim_arguments_error() {
    local progname="$1"
    local error="$2"
    local summary="$3"

    echo $progname: Error: $error
    _evssim_arguments_usage "$progname" "$summary"
    exit 2
}

# A check for most builder scripts' argument count.
# Checks that there is only one argument, and that the argument is a valid
# docker version, if not displays a standard error message + Usage.
# Parameters:
#  - progname - The script's name/path (its $0)
#  - version - The docker version(:tag) argument given to the program
#  - argc - The number of arguments given to the script ($#)
#  - summary - An optional summary line to print under "Usage: "
evssim_validate_version_arguments() {
    local progname=$(basename "$1")
    local version="$2"
    local argc="$3"
    local summary="${4:-}"
    local valid_versions="$(docker image ls "$EVSSIM_DOCKER_IMAGE_NAME" --format '{{.Tag}}')"
    local is_valid_version="false"

    for ver in $valid_versions; do
        if [ "$ver" = "$version" ]; then
            is_valid_version="true"
            break
        fi
    done

    if [ "$argc" -ne 1 ]; then
        _evssim_arguments_error "$progname" "Wrong number of arguments." "$summary"
    elif [ "$is_valid_version" = "false" ]; then
        _evssim_arguments_error "$progname" "Invalid version '$version'." "$summary"
    fi
}

# Return VSSIM related environment variables
# Parameters - None
evssim_all_env () {
    env | grep "^EVSSIM_"
}

# Execute qemu in attached or detached mode
# Parameters
#  - attachness - "attached" or "detached"
#  - ssd - VSSIM ssd configuration string
#  - image - QEMU image path
#  - bios - BIOS image path
#  - kernel - Kernel image path
#  - initrd - Init fs path
#  - append - Kernel options. Usually configuration of boot disk.
#  - host_version - Host docker image version to run qemu from.
evssim_qemu () {
    local attached=$1
    local ssd=$2
    local image=$3
    local bios=$4
    local kernel=$5
    local initrd=$6
    local append=$7
    local host_version=$8

    # Max timeout of the docker before we force quit
    local timeout=""
    if [ "$attached" != "attached" ]; then
        timeout="timeout $EVSSIM_DOCKER_MAX_TIMEOUT_IN_MINUTES"m
    fi

    # Trace configuration
    local trace_config="";
    if [[ "$EVSSIM_QEMU_TRACE_NVME" =~ y.* ]]; then
        trace_config="$trace_config -trace \"nvme*\""
    fi
    if [[ "$EVSSIM_QEMU_TRACE_VSSIM" =~ y.* ]]; then
        trace_config="$trace_config -trace \"vssim*\""
    fi
    if [[ "$EVSSIM_QEMU_TRACE_BLOCK" =~ y.* ]]; then
        trace_config="$trace_config -trace \"bdrv*\" -trace \"blk*\""
    fi

    # Build drive and device arguments
    local drive_args=""
    local device_args=""
    local device_simulator="off";
    local device_size=$EVSSIM_QEMU_DEFAULT_DISK_SIZE

    # Simulator state
    if [[ "$EVSSIM_QEMU_SIMULATOR_ENABLED" =~ y.* ]]; then
        device_simulator="on";
        echo "Starting simulator mode"

        local device_count=($(evssim_get_device_count))
        local device_sizes=($(evssim_calculate_ssd_conf_disk_sizes))

        if [ ${#device_sizes[@]} -eq 0 ]; then
            echo "ERROR: No devices found in SSD configuration"
            exit 1
        fi

        # Multi-device mode with real configuration
        local serial_number=1
        local device_index=0
        for device_size in "${device_sizes[@]}"; do
            # Create drive argument
            drive_args="$drive_args -drive format=vssim,size=$device_size,simulator=$device_simulator,if=none,id=memory$serial_number,device_index=$device_index"

            # Create NVMe device argument
            device_args="$device_args -device nvme,drive=memory$serial_number,serial=$serial_number"

            ((++device_index))
            ((++serial_number))
        done

        echo "INFO Simulator mode ($device_count devices)"
        local serial_number=1
        for device_size in "${device_sizes[@]}"; do
            echo "     Device $serial_number: $(numfmt --from=iec --to=iec $device_size)"
            ((serial_number++))
        done
    else
        # Non-simulator mode - use default size
        drive_args="-drive format=vssim,size=$device_size,simulator=$device_simulator,if=none,id=memory,device_index=0 -drive format=vssim,size=$device_size,simulator=$device_simulator,if=none,id=memory2,device_index=1 -drive format=vssim,size=$device_size,simulator=$device_simulator,if=none,id=memory3,device_index=2"
        device_args="-device nvme,drive=memory,serial=1 -device nvme,drive=memory2,serial=2 -device nvme,drive=memory3,serial=3"
        echo "INFO Non-simulator mode, Default size: $(numfmt --from=iec --to=iec $device_size)"
    fi

    # Build the complete args
    local args="cd $EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_QEMU_FOLDER/hw && $timeout ../x86_64-softmmu/qemu-system-x86_64 -rtc base=localtime,clock=host -pidfile /tmp/qemu.pid $trace_config -m 4G -smp 4 -drive format=qcow2,file=$image $drive_args $device_args -device e1000,netdev=net0 -netdev user,id=net0,hostfwd=tcp::$EVSSIM_QEMU_SSH_PORT-:22 -vnc :$EVSSIM_QEMU_VNC -machine accel=kvm -kernel $kernel -initrd $initrd -L /usr/share/seabios -L ../pc-bios/optionrom -append '$append'";

    # Stop any previous runs
    evssim_qemu_stop

    echo "INFO Starting QEMU ($attached) with args = $args"

    # Clear previous runtime data
    if [[ "$EVSSIM_RUNTIME_ALWAYS_RESET" =~ y.* ]]; then
        find "$EVSSIM_DATA_FOLDER" -name "*.dat" -delete
    fi

    # Build ssd configuration
    echo "$ssd" > $EVSSIM_DATA_FOLDER/ssd.conf

    case "$attached" in
        attached)
            docker run --rm -i $docker_extra_tty --net=host $EVSSIM_DOCKER_XOPTIONS --privileged --env-file <(evssim_all_env) -v $EVSSIM_ROOT_PATH/$EVSSIM_DATA_FOLDER:$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_QEMU_FOLDER/hw/data -v $EVSSIM_ROOT_PATH:$EVSSIM_DOCKER_ROOT_PATH "$EVSSIM_DOCKER_IMAGE_NAME:$host_version" bash -c "$args"
            ;;
        *)
            export EVSSIM_DOCKER_UUID=$(docker run --rm -d --net=host $EVSSIM_DOCKER_XOPTIONS --privileged --env-file <(evssim_all_env) -v $EVSSIM_ROOT_PATH/$EVSSIM_DATA_FOLDER:$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_QEMU_FOLDER/hw/data -v $EVSSIM_ROOT_PATH:$EVSSIM_DOCKER_ROOT_PATH "$EVSSIM_DOCKER_IMAGE_NAME:$host_version" bash -c "$args")
            echo INFO Docker started $EVSSIM_DOCKER_UUID
            trap "evssim_qemu_stop" EXIT SIGTERM SIGINT
            sleep 1
            ;;
    esac
}

# Flush the disk from inside the qemu running virtual machine
# Parameters - None
evssim_qemu_flush_disk () {
    evssim_guest sync
}

# Stop detached qemu run
# Will first soft kill the qemu and if fails, will force kill it.
# Parameters - None
evssim_qemu_stop () {
    if [ ! -z ${EVSSIM_DOCKER_UUID:-} ]; then
        if docker ps -q --no-trunc | grep $EVSSIM_DOCKER_UUID > /dev/null; then
            # Kill qemu safely
            local code=$(cat <<DOCKER
kill -SIGTERM \$(cat /tmp/qemu.pid)
timeout 10 tail --pid \$(cat /tmp/qemu.pid) -f /dev/null
DOCKER
)
            set +e
            docker exec --privileged $EVSSIM_DOCKER_UUID /bin/bash -c "$code"
            sleep 1
            set -e

            # Force kill no, wait
            if docker ps -q --no-trunc | grep $EVSSIM_DOCKER_UUID > /dev/null; then
                docker ps -q --no-trunc | grep $EVSSIM_DOCKER_UUID
                echo "TOOT"
                docker stop -t 0 $EVSSIM_DOCKER_UUID > /dev/null
                echo WARNING Killed docker $EVSSIM_DOCKER_UUID
            fi
        fi
        export EVSSIM_DOCKER_UUID=""
    fi
}

# Common function running qemu
# Will run qemu with default parameters
# Parameters
#  - attachness - "attached" or "detached"
#  - host_version - Host docker image version to run qemu from.
evssim_qemu_default () {
    local attached="$1"
    local host_version="$2"
    evssim_qemu "$attached" \
                "$(evssim_build_ssd_conf)" \
                "$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/$EVSSIM_QEMU_IMAGE" \
                "$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_SIMULATOR_FOLDER/infra/ansible/roles/guest_tester_pre/files/bios.bin" \
                "$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/kernel/vmlinuz-$EVSSIM_KERNEL_DIST" \
                "$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/kernel/initrd.img-$EVSSIM_KERNEL_DIST" \
                "root=/dev/disk/by-label/cloudimg-rootfs ro" \
		"$host_version"
}

# Run QEMU attached to console
# Parameters
#  - host_version - Host docker image version to run qemu from.
evssim_qemu_attached () {
    local host_version="$1"
    evssim_qemu_default attached "$host_version"
}

# Run QEMU detached from console. Use evssim_qemu_stop to stop.
# Parameters
#  - host_version - Host docker image version to run qemu from.
evssim_qemu_detached () {
    local host_version="$1"
    evssim_qemu_default detached "$host_version"
}

# Use fresh image of qemu
# Parameters
#  - host_version - Host docker image version to run from.
evssim_qemu_fresh_image () {
    local host_version="$1"
    if [ ! -f $IMAGE_PATH ]; then
        echo "ERROR QEMU Image is missing. Please run ./build-qemu-image.sh first."
        exit 1;
    fi

    cp -f $IMAGE_PATH_TEMPLATE $IMAGE_PATH

    # Copy tools inside
    evssim_copy_tools "$host_version"
}

# Copy NVME tools, exofs tool and OSD emulator into the QEMU image
# Copy into an offline image using mounting of the qemu image.
# Parameters
#  - host_version - Host docker image version to run from.
# Example
#   evssim_copy_tools
evssim_copy_tools () {
    local host_version="$1"
    evssim_run "$host_version" "sudo guestfish -a '$INTERNAL_IMAGE_PATH' -i << EOF
    command \"mkdir -p '$EVSSIM_GUEST_ROOT_PATH/guest'\"
    copy-in '$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/nvme' '$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/tnvme' '$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/dnvme.ko' '$EVSSIM_GUEST_ROOT_PATH/guest/'
    copy-in '$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_SIMULATOR_FOLDER/eVSSIM/tests/guest/' '$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/osc-osd' '$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/exofs' '$EVSSIM_GUEST_ROOT_PATH'
    copy-in '$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/libosd.so' '/lib/'
    copy-in '$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/mkfs.exofs' '/bin/'
    copy-in '$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/kernel/lib/' '/'
EOF"
}

# Execute guest command inside running QEMU virtual machine.
# Uses ssh and the integrated public key to execute the command.
# Parameters - Command to execute
# Example
#   evssim_guest ls -al
evssim_guest () {
    ssh_extra_tty=""
    if [ -t 0 ]; then
        ssh_extra_tty=-t
    fi
    ssh -q $ssh_extra_tty -i "$EVSSIM_SSH_KEYS_PATH/id_ed25519" -i "$EVSSIM_SSH_KEYS_PATH/id_rsa" -p $EVSSIM_QEMU_SSH_PORT -o ConnectionAttempts=1024 -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no -o PasswordAuthentication=no -o PubkeyAcceptedKeyTypes=+ssh-rsa,ssh-ed25519 $EVSSIM_QEMU_UBUNTU_USERNAME@localhost bash -c \"$@\"
}

evssim_copy_from_guest () {
    local DOCKER_FILE_PATH=$1
    local OUTPUT_FILE_PATH=$2
    scp -r -o ConnectionAttempts=1024 -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no -o PasswordAuthentication=no -o PubkeyAcceptedKeyTypes=+ssh-rsa,ssh-ed25519 -i "$EVSSIM_SSH_KEYS_PATH/id_ed25519" -i "$EVSSIM_SSH_KEYS_PATH/id_rsa" -P $EVSSIM_QEMU_SSH_PORT $EVSSIM_QEMU_UBUNTU_USERNAME@localhost:$DOCKER_FILE_PATH $OUTPUT_FILE_PATH
}
