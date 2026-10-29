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

# Calculate sizes for nvme devices only (skip namespace sections like [ns01])
for device_name in sorted(devices.keys()):
    if not device_name.startswith('nvme'):
        continue
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
    if line.startswith('[nvme') and line.endswith(']'):
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

    if [ "$host_version" != "$EVSSIM_QEMU_COMPILE_CONTAINER" ]; then
        echo "ERROR Runtime container $host_version conflicts with configuration $EVSSIM_VERSIONS_CONFIGURATION_ID"
        return 1
    fi

    local runtime_image="$EVSSIM_DOCKER_IMAGE_NAME:$host_version"
    local runtime_workdir="$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_QEMU_FOLDER/hw"
    local runtime_executable="../x86_64-softmmu/qemu-system-x86_64"
    local firmware_args="-L /usr/share/seabios -L ../pc-bios/optionrom"
    local runtime_selection=""
    local runtime_mounts=(-v "$EVSSIM_ROOT_PATH/$EVSSIM_DATA_FOLDER:$runtime_workdir/data")
    if [ -n "${EVSSIM_QEMU_ARTIFACT_SELECTION:-}" ]; then
        # S2 emits this JSON selection. Verify it against the loaded profile;
        # never substitute a mutable tag or the last binary built in qemu/.
        if ! runtime_selection=$(python3 "$EVSSIM_ROOT_PATH/$EVSSIM_BUILDER_FOLDER/qemu-artifact.py" \
            --runtime-selection "$EVSSIM_QEMU_ARTIFACT_SELECTION" \
            --profiles "$EVSSIM_ROOT_PATH/$EVSSIM_VERSIONS_FOLDER/configs.json"); then
            return 1
        fi
        runtime_image=$(jq -er '.container_image_id' <<< "$runtime_selection")
        runtime_executable=$(jq -er '.runtime_executable' <<< "$runtime_selection")
        local runtime_data_dir
        local staged_bundle
        local monitor_assets
        runtime_data_dir=$(jq -er '.runtime_data_dir' <<< "$runtime_selection")
        staged_bundle=$(jq -er '.staged_bundle' <<< "$runtime_selection")
        monitor_assets=$(jq -er '.vssim_www_dir' <<< "$runtime_selection")
        printf -v firmware_args '%s %q' '-L' "$runtime_data_dir"
        runtime_workdir=/qemu-runtime
        # Writable data stays outside the immutable bundle. The temporary
        # working directory preserves ./data/ssd.conf and both monitor layouts.
        runtime_mounts=(--tmpfs /qemu-runtime \
            -v "$staged_bundle:/qemu-artifact:ro" \
            -v "$EVSSIM_ROOT_PATH/$EVSSIM_DATA_FOLDER:/qemu-runtime/data" \
            -v "$monitor_assets:/qemu-runtime/vssim/simulator/www:ro" \
            -v "$monitor_assets:/qemu-runtime/www:ro")
        echo "INFO Selected QEMU artifact: $(jq -cr '{key,profile_id,version,binary_sha256,sources}' <<< "$runtime_selection")"
    elif [ "$EVSSIM_QEMU_BRANCH" != master ]; then
        echo "ERROR QEMU $EVSSIM_QEMU_BRANCH requires EVSSIM_QEMU_ARTIFACT_SELECTION from S2"
        return 1
    fi

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

        local device_count
        local rendered_sizes
        if ! device_count=$(evssim_get_device_count) || \
                ! rendered_sizes=$(evssim_calculate_ssd_conf_disk_sizes); then
            echo "ERROR: Cannot read devices from SSD configuration"
            return 1
        fi
        local device_sizes=($rendered_sizes)

        # Preserve the existing parsers, but do not launch a partial or empty
        # device set if their count/size results disagree.
        if [[ ! "$device_count" =~ ^[1-9][0-9]*$ ]] || \
                [ "$device_count" -ne "${#device_sizes[@]}" ]; then
            echo "ERROR: SSD device count and calculated sizes disagree"
            return 1
        fi
        for device_size in "${device_sizes[@]}"; do
            if [[ ! "$device_size" =~ ^[1-9][0-9]*$ ]]; then
                echo "ERROR: Invalid calculated SSD size: $device_size"
                return 1
            fi
        done

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

        EVSSIM_PROVISIONED_DEVICE_COUNT=${#device_sizes[@]}

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
        EVSSIM_PROVISIONED_DEVICE_COUNT=3
        echo "INFO Non-simulator mode, Default size: $(numfmt --from=iec --to=iec $device_size)"
    fi

    # Build the complete args
    local quoted_workdir
    local quoted_executable
    printf -v quoted_workdir '%q' "$runtime_workdir"
    printf -v quoted_executable '%q' "$runtime_executable"
    local args="cd $quoted_workdir && $timeout $quoted_executable -rtc base=localtime,clock=host -pidfile /tmp/qemu.pid $trace_config -m 4G -smp 4 -drive format=qcow2,file=$image $drive_args $device_args -device e1000,netdev=net0 -netdev user,id=net0,hostfwd=tcp::$EVSSIM_QEMU_SSH_PORT-:22 -vnc :$EVSSIM_QEMU_VNC -machine accel=kvm -kernel $kernel -initrd $initrd $firmware_args -append '$append'";

    # Stop any previous runs
    if ! evssim_qemu_stop; then
        return 1
    fi

    echo "INFO Starting QEMU ($attached) with args = $args"

    # Clear previous runtime data
    if [[ "$EVSSIM_RUNTIME_ALWAYS_RESET" =~ y.* ]]; then
        if ! find "$EVSSIM_DATA_FOLDER" -name "*.dat" -delete; then
            echo "ERROR Cannot clear previous QEMU runtime data"
            return 1
        fi
    fi

    # Build ssd configuration
    if ! echo "$ssd" > "$EVSSIM_DATA_FOLDER/ssd.conf"; then
        echo "ERROR Cannot write the launch SSD configuration"
        return 1
    fi
    # Keep the verified selection in this shell for the detached PID check.
    EVSSIM_QEMU_LAUNCH_SELECTION="$runtime_selection"

    case "$attached" in
        attached)
            docker run --rm -i $docker_extra_tty --net=host $EVSSIM_DOCKER_XOPTIONS --privileged --env-file <(evssim_all_env) "${runtime_mounts[@]}" -v $EVSSIM_ROOT_PATH:$EVSSIM_DOCKER_ROOT_PATH "$runtime_image" bash -c "$args"
            ;;
        *)
            # export would mask docker run's failure status. Capture first so
            # a rejected launch cannot proceed to readiness or guest tests.
            # Keep an exited container until cleanup so startup logs survive.
            if ! EVSSIM_DOCKER_UUID=$(docker run -d --net=host $EVSSIM_DOCKER_XOPTIONS --privileged --env-file <(evssim_all_env) "${runtime_mounts[@]}" -v $EVSSIM_ROOT_PATH:$EVSSIM_DOCKER_ROOT_PATH "$runtime_image" bash -c "$args"); then
                echo "ERROR Docker failed to launch QEMU"
                return 1
            fi
            export EVSSIM_DOCKER_UUID
            echo INFO Docker started $EVSSIM_DOCKER_UUID
            # Signals must end the run. Cleanup preserves an earlier failure,
            # and a cleanup failure must also fail an otherwise successful run.
            trap 'qemu_exit_status=$?
                if ! evssim_qemu_stop && [ "$qemu_exit_status" -eq 0 ]; then qemu_exit_status=1; fi
                exit "$qemu_exit_status"' EXIT
            trap 'exit 143' SIGTERM
            trap 'exit 130' SIGINT
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
    local container="${EVSSIM_DOCKER_UUID:-}"
    if [ -z "$container" ]; then
        return 0
    fi
    local existing
    if ! existing=$(docker ps -aq --no-trunc --filter "id=$container"); then
        echo "ERROR Cannot inspect QEMU container during cleanup: $container"
        return 1
    fi
    if [ -n "$existing" ]; then
        # Retain the graceful QEMU stop, then remove the container even when
        # its PID file/process is gone. Do not change the caller's set -e mode.
        if ! docker exec --privileged "$container" /bin/bash -c '
            pid=$(cat /tmp/qemu.pid) &&
            [[ "$pid" =~ ^[1-9][0-9]*$ ]] &&
            kill -SIGTERM "$pid" && timeout 10 tail --pid "$pid" -f /dev/null'; then
            echo "WARNING QEMU graceful stop unavailable; removing container $container"
        fi
        if ! docker rm -f "$container" >/dev/null 2>&1; then
            # --rm can remove it between the graceful stop and docker rm.
            if ! existing=$(docker ps -aq --no-trunc --filter "id=$container") || [ -n "$existing" ]; then
                echo "ERROR Failed to remove QEMU container: $container"
                return 1
            fi
        fi
    fi
    export EVSSIM_DOCKER_UUID=""
}

# Common function running qemu
# Will run qemu with default parameters
# Parameters
#  - attachness - "attached" or "detached"
#  - host_version - Host docker image version to run qemu from.
evssim_qemu_default () {
    local attached="$1"
    local host_version="$2"
    local ssd
    # Command substitution in an argument would hide a failed template render,
    # especially in simulator-off mode where the size parser is not called.
    if ! ssd=$(evssim_build_ssd_conf) || [ -z "$ssd" ]; then
        echo "ERROR Cannot render the launch SSD configuration"
        return 1
    fi
    evssim_qemu "$attached" \
                "$ssd" \
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
    if ! evssim_qemu_default detached "$host_version"; then
        return 1
    fi

    # hostfwd accepts the moment QEMU starts, so ssh cannot retry its way in
    if evssim_wait_for_guest && {
        # Recheck the frozen record, not a selection file that may have changed
        # during boot. PID, command line, binary hash and image must agree.
        [ -z "${EVSSIM_QEMU_LAUNCH_SELECTION:-}" ] ||
            jq 'del(.runtime_executable, .runtime_data_dir)' <<< "$EVSSIM_QEMU_LAUNCH_SELECTION" | \
                python3 "$EVSSIM_ROOT_PATH/$EVSSIM_BUILDER_FOLDER/qemu-artifact.py" \
                --runtime-selection /dev/stdin --running-container "$EVSSIM_DOCKER_UUID" \
                --profiles "$EVSSIM_ROOT_PATH/$EVSSIM_VERSIONS_FOLDER/configs.json" >/dev/null
    } && evssim_wait_for_devices; then
        return 0
    fi

    # Capture startup output before cleanup removes the failed container.
    if ! docker logs --tail 50 "$EVSSIM_DOCKER_UUID" >&2; then
        echo "WARNING QEMU container logs are unavailable"
    fi
    if ! evssim_qemu_stop; then
        echo "ERROR QEMU startup cleanup failed"
    fi
    return 1
}

# Use fresh image of qemu
# Parameters
#  - host_version - Host docker image version to run from.
#  - tool_group - core or exofs; omitted preserves the existing full payload.
evssim_qemu_fresh_image () {
    local host_version="$1"
    local tool_group="${2:-exofs}"
    if [ ! -f "$IMAGE_PATH_TEMPLATE" ]; then
        echo "ERROR QEMU image template is missing. Please run ./build-qemu-image.sh first."
        return 1
    fi
    # Reject missing inputs before replacing the image or starting injection.
    if ! evssim_validate_guest_payload "$tool_group"; then
        return 1
    fi

    # Image injection is offline: stop the previous guest before replacement.
    evssim_qemu_stop
    cp -f "$IMAGE_PATH_TEMPLATE" "$IMAGE_PATH"

    # Copy tools inside
    evssim_copy_tools "$host_version" "$tool_group"
}

# Core preparation must not depend on outputs from the separate exofs build.
evssim_validate_guest_payload () {
    local tool_group="$1"
    case "$tool_group" in
        core|exofs) ;;
        *) echo "ERROR Unknown guest tool group: $tool_group (expected core or exofs)"; return 1 ;;
    esac

    local dist="$EVSSIM_ROOT_PATH/$EVSSIM_DIST_FOLDER"
    local payload
    local required_files=("$dist/nvme" "$dist/tnvme" "$dist/dnvme.ko")
    local required_directories=("$EVSSIM_ROOT_PATH/$EVSSIM_SIMULATOR_FOLDER/eVSSIM/tests/guest" "$dist/kernel/lib")
    if [ "$tool_group" = exofs ]; then
        required_files+=("$dist/libosd.so" "$dist/mkfs.exofs")
        required_directories+=("$dist/osc-osd" "$dist/exofs")
    fi
    for payload in "${required_files[@]}"; do
        if [ ! -f "$payload" ]; then
            echo "ERROR Missing $tool_group guest payload file: $payload"
            return 1
        fi
    done
    for payload in "${required_directories[@]}"; do
        if [ ! -d "$payload" ]; then
            echo "ERROR Missing $tool_group guest payload directory: $payload"
            return 1
        fi
    done
}

# Copy the core payload and, when selected, existing exofs outputs.
# Copy into an offline image using mounting of the qemu image.
# Parameters
#  - host_version - Host docker image version to run from.
#  - tool_group - core or exofs; omitted preserves the existing full payload.
# Example
#   evssim_copy_tools
evssim_copy_tools () {
    local host_version="$1"
    local tool_group="${2:-exofs}"
    if ! evssim_validate_guest_payload "$tool_group"; then
        return 1
    fi
    local exofs_commands=""
    if [ "$tool_group" = exofs ]; then
        exofs_commands="    copy-in '$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/osc-osd' '$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/exofs' '$EVSSIM_GUEST_HOME_PATH'
    copy-in '$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/libosd.so' '/lib'
    copy-in '$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/mkfs.exofs' '/bin'"
    fi
    evssim_run "$host_version" "sudo guestfish -a '$INTERNAL_IMAGE_PATH' -i << EOF
    command \"mkdir -p '$EVSSIM_GUEST_HOME_PATH/guest'\"
    copy-in '$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/nvme' '$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/tnvme' '$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/dnvme.ko' '$EVSSIM_GUEST_HOME_PATH/guest/'
    copy-in '$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_SIMULATOR_FOLDER/eVSSIM/tests/guest/' '$EVSSIM_GUEST_HOME_PATH'
$exofs_commands
    copy-in '$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/kernel/lib/' '/'
EOF"
}

# A running container can still contain a dead QEMU process. A missing PID
# file is allowed while QEMU is starting; readiness still has a deadline.
evssim_qemu_is_running () {
    if [ -z "${EVSSIM_DOCKER_UUID:-}" ]; then
        return 1
    fi
    local running
    if ! running=$(docker inspect --format '{{.State.Running}}' "$EVSSIM_DOCKER_UUID") || [ "$running" != true ]; then
        return 1
    fi
    docker exec "$EVSSIM_DOCKER_UUID" /bin/bash -c '
        if [ ! -f /tmp/qemu.pid ]; then exit 0; fi
        read -r pid < /tmp/qemu.pid &&
        [[ "$pid" =~ ^[1-9][0-9]*$ ]] && kill -0 "$pid"' 2>/dev/null
}

# Startup probes use one bounded SSH attempt. The general guest-command
# helper retains its existing retry policy for the test suites.
evssim_guest_probe () {
    local remaining="$1"
    shift
    if [ "$remaining" -gt 5 ]; then remaining=5; fi
    timeout "$remaining" ssh -q -o BatchMode=yes -o ConnectionAttempts=1 -o ConnectTimeout=3 \
        -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no -o PasswordAuthentication=no \
        -o PubkeyAcceptedKeyTypes=+ssh-rsa,ssh-ed25519 \
        -i "$EVSSIM_SSH_KEYS_PATH/id_ed25519" -i "$EVSSIM_SSH_KEYS_PATH/id_rsa" \
        -p "$EVSSIM_QEMU_SSH_PORT" "$EVSSIM_QEMU_UBUNTU_USERNAME@localhost" "$@"
}

# Wait for SSH within a wall-clock deadline, detecting QEMU exit each poll.
# Parameters - [max_wait] (default 300 seconds)
evssim_wait_for_guest () {
    local max_wait=${1:-300}
    local started=$SECONDS
    local deadline=$((started + max_wait))
    local remaining
    echo "INFO Waiting for VM to boot (up to ${max_wait}s)..."
    while [ "$SECONDS" -lt "$deadline" ]; do
        if ! evssim_qemu_is_running; then
            echo "ERROR QEMU exited while waiting for guest SSH"
            return 1
        fi
        remaining=$((deadline - SECONDS))
        if [ "$remaining" -le 0 ]; then break; fi
        if evssim_guest_probe "$remaining" /bin/true 2>/dev/null; then
            echo "INFO VM SSH ready after $((SECONDS - started))s"
            return 0
        fi
        remaining=$((deadline - SECONDS))
        if [ "$remaining" -gt 5 ]; then remaining=5; fi
        if [ "$remaining" -gt 0 ]; then sleep "$remaining"; fi
    done
    echo "ERROR VM SSH not available after ${max_wait}s"
    return 1
}

# Wait for an NVMe device to appear inside the running guest VM.
# Polls every 2 seconds up to max_wait seconds (default 60).
# Assumes the guest is already reachable (call evssim_wait_for_guest first).
# Parameters - [device] [max_wait]
evssim_wait_for_device () {
    local device=${1:-/dev/nvme0n1}
    local max_wait=${2:-60}
    local interval=2
    local elapsed=0
    echo "INFO Waiting for NVMe device $device (up to ${max_wait}s)..."
    while ! evssim_guest ls -al "$device" 2>/dev/null >/dev/null; do
        elapsed=$((elapsed + interval))
        if [ $elapsed -ge $max_wait ]; then
            echo "ERROR NVMe device $device not available after ${max_wait}s"
            return 1
        fi
        sleep $interval
    done
    echo "INFO NVMe device $device ready after ${elapsed}s"
}

# Require exactly the namespaces corresponding to the arguments just rendered
# by evssim_qemu, including all three existing simulator-off devices.
# Parameters - [max_wait] (default 60 seconds for the whole device set)
evssim_wait_for_devices () {
    local max_wait=${1:-60}
    local started=$SECONDS
    local deadline=$((started + max_wait))
    local remaining
    local expected=()
    local index
    for ((index=0; index<EVSSIM_PROVISIONED_DEVICE_COUNT; index++)); do
        expected+=("/dev/nvme${index}n1")
    done
    if [ "${#expected[@]}" -eq 0 ]; then
        echo "ERROR No expected NVMe devices were provisioned"
        return 1
    fi
    local expected_inventory
    expected_inventory=$(printf '%s\n' "${expected[@]}" | LC_ALL=C sort)
    local inventory=""
    local device
    echo "INFO Waiting for NVMe devices: ${expected[*]} (up to ${max_wait}s)..."
    while [ "$SECONDS" -lt "$deadline" ]; do
        if ! evssim_qemu_is_running; then
            echo "ERROR QEMU exited while waiting for NVMe devices"
            return 1
        fi
        remaining=$((deadline - SECONDS))
        if [ "$remaining" -le 0 ]; then break; fi
        # /sys/block excludes partitions. Require real block nodes, not stale
        # names; a second namespace is an unexpected device in this topology.
        if inventory=$(evssim_guest_probe "$remaining" '
            for path in /sys/block/nvme*n*; do
                name=${path##*/}
                if [ -b "/dev/$name" ]; then printf "/dev/%s\n" "$name"; fi
            done' 2>/dev/null); then
            inventory=$(LC_ALL=C sort <<< "$inventory")
            for device in $inventory; do
                if [[ " ${expected[*]} " != *" $device "* ]]; then
                    echo "ERROR Unexpected NVMe device: $device; expected ${expected[*]}"
                    return 1
                fi
            done
            if [ "$inventory" = "$expected_inventory" ]; then
                echo "INFO NVMe inventory verified after $((SECONDS - started))s: ${expected[*]}"
                return 0
            fi
        fi
        remaining=$((deadline - SECONDS))
        if [ "$remaining" -gt 2 ]; then remaining=2; fi
        if [ "$remaining" -gt 0 ]; then sleep "$remaining"; fi
    done
    echo "ERROR NVMe inventory did not match after ${max_wait}s; expected ${expected[*]}; observed $inventory"
    return 1
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
