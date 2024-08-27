#!/bin/bash
set -e

declare -x EVSSIM_DOCKER_UUID

# Verify environment is loaded
if [ -z $EVSSIM_ENVIRONMENT ]; then
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
if [ -z $EVSSIM_ENV_PATH ]; then
    echo "ERORR Missing environment file path"
    exit 1
elif [[ $(md5sum $EVSSIM_ENV_PATH | cut -d " " -f 1) != $EVSSIM_ENV_HASH ]]; then
    echo "WARNING Environment file hash changed. Please reload using 'source ./env.sh'"
fi

# Check for docker support
if ! which docker >/dev/null; then
    echo "ERORR Missing docker configuration"
    exit 1
fi

if ! docker ps 2>/dev/null >/dev/null; then
    echo "ERORR Docker has no permissions. Consider adding user to docker group. Logout and login afterwards."
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
chmod 600 $EVSSIM_ROOT_PATH/$EVSSIM_BUILDER_FOLDER/docker/id_rsa

# Link dist folder
for folder in $EVSSIM_DATA_LINKED_FOLDER; do
    if [ ! -L $folder ]; then
        ln -rs $EVSSIM_ROOT_PATH/$EVSSIM_DATA_FOLDER $folder
    fi
done

# Run new instance of docker in a specific root folder
# Parameters
#  - folder - Folder name under the project root
#  - command - Command to execute
evssim_run_at_folder () {
    evssim_run_at_path $EVSSIM_DOCKER_ROOT_PATH/$1 "${@:2}"
}

# Run new instance of docker in a specific path
# Parameters
#  - path - Path inside docker
#  - command - Command to execute
evssim_run_at_path () {
    local path=$1
    local args="${@:2}"
    docker run --rm -i $docker_extra_tty $EVSSIM_DOCKER_XOPTIONS $EVSSIM_DOCKER_PORTS_OPTION --privileged --env-file <(evssim_all_env) -v $EVSSIM_ROOT_PATH:$EVSSIM_DOCKER_ROOT_PATH $EVSSIM_DOCKER_IMAGE_NAME bash -c "cd $path; $args"
}

# Run
evssim_run () {
    evssim_run_at_path $EVSSIM_DOCKER_ROOT_PATH "$@"
}

# Run new instance of docker and chroot into the disk image
# Parameters - Command to run
evssim_run_chrooted () {
    local mount_point=/mnt/guest

    # Verify the disk exists
    if [ ! -f $IMAGE_PATH ]; then
        echo "ERROR Missing qemu image file. Run 'build-qemu-image.sh'"
        exit 1
    fi

    # Run inside th chroot
    local mount_point=/mnt/guest
    local image_path=$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/$EVSSIM_QEMU_IMAGE
    EVSSIM_RUN_SUDO=y evssim_run "mkdir -p $mount_point && mount -o loop $image_path $mount_point && chroot $mount_point $@"
}

# Run new instance of the docker and mount offline the disk image
# Parameters - Command to run
evssim_run_mounted () {
    local mount_point=/mnt/guest

    # Verify the disk exists
    if [ ! -f $IMAGE_PATH ]; then
        echo "ERROR Missing qemu image file. Run 'build-qemu-image.sh'"
        exit 1
    fi

    # Execute while mounted
    local mount_point=/mnt/guest
    local image_path=$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/$EVSSIM_QEMU_IMAGE
    evssim_run "sudo mkdir -p $mount_point && sudo mount -o loop $image_path $mount_point && cd $mount_point/$EVSSIM_GUEST_ROOT_PATH && bash -c \"$@\""
}

# Build SSD configuration from environment
# Parameters - None
evssim_build_ssd_conf () {
    python -c "import os; import sys; sys.stdout.write(open('$EVSSIM_RUNTIME_SSD_CONF_TEMPLATE', 'rt').read() % os.environ)"
}

# Calculate ssd disk size from ssd configuration
# Parameters - None
evssim_calculate_ssd_conf_disk_size() {
    local code=$(cat <<PYTHON
# Parse ssd configuration
import sys
data=dict(x.strip().split(' ', 1) for x in sys.stdin.readlines())
def g(name): return int(data[name])
# Calculate the final size (With casts)
print(g("FLASH_NB")*g("BLOCK_NB")*g("PAGE_NB")*g("PAGE_SIZE"))
PYTHON
)
    echo "$ssd" | python -c "$code"
}


# Return VSSIM related environment variables
# Parameters - None
evssim_all_env () {
    env | grep "^EVSSIM_"
}

# Execute qemu in atached or detached mode
# Parameters
#  - attachness - "attached" or "detached"
#  - ssd - VSSIM ssd configuration string
#  - image - QEMU image path
#  - bios - BIOS image path
#  - kernel - Kernel image path
#  - initrd - Init fs path
#  - append - Kernel options. Usually configuration of boot disk.
evssim_qemu () {
    local attached=$1
    local ssd=$2
    local image=$3
    local bios=$4
    local kernel=$5
    local initrd=$6
    local append=$7

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

    # Simulator state
    local device_simulator="off";
    local device_size=$EVSSIM_QEMU_DEFAULT_DISK_SIZE;
    if [[ "$EVSSIM_QEMU_SIMULATOR_ENABLED" =~ y.* ]]; then
        device_simulator="on";
        device_size=$(evssim_calculate_ssd_conf_disk_size)
    fi

    local device_size_formatted=$(numfmt --from=iec --to=iec $device_size)
    if [ -z "$device_size_formatted" ]; then
        echo "ERROR Invalid disk size format"
        exit 1
    fi;

    echo "INFO Simulator ($device_simulator), Disk Size $device_size_formatted"

    local args="cd $EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_QEMU_FOLDER/hw && $timeout ../x86_64-softmmu/qemu-system-x86_64 -rtc base=localtime,clock=host -pidfile /tmp/qemu.pid $trace_config -m 4096 -smp 4 -drive format=raw,file=$image -drive format=vssim,size=$device_size,simulator=$device_simulator,if=none,id=memory -device nvme,drive=memory,serial=1 -device e1000,netdev=net0 -netdev user,id=net0,hostfwd=tcp::$EVSSIM_QEMU_PORT-:22 -vnc :$EVSSIM_QEMU_VNC -machine accel=kvm -kernel $kernel -initrd $initrd -L /usr/share/seabios -L ../pc-bios/optionrom -append '$append'";

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
            docker run --rm -i $docker_extra_tty --net=host $EVSSIM_DOCKER_XOPTIONS --privileged --env-file <(evssim_all_env) -v $EVSSIM_ROOT_PATH/$EVSSIM_DATA_FOLDER:$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_QEMU_FOLDER/hw/data -v $EVSSIM_ROOT_PATH:$EVSSIM_DOCKER_ROOT_PATH $EVSSIM_DOCKER_IMAGE_NAME bash -c "cd $path; $args"
            ;;
        *)
            export EVSSIM_DOCKER_UUID=$(docker run --rm -d --net=host $EVSSIM_DOCKER_XOPTIONS --privileged --env-file <(evssim_all_env) -v $EVSSIM_ROOT_PATH/$EVSSIM_DATA_FOLDER:$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_QEMU_FOLDER/hw/data -v $EVSSIM_ROOT_PATH:$EVSSIM_DOCKER_ROOT_PATH $EVSSIM_DOCKER_IMAGE_NAME bash -c "cd $path; $args")
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
    if [ ! -z $EVSSIM_DOCKER_UUID ]; then
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
#   attachness - "attached" or "detached"
evssim_qemu_default () {
    local image_path=$EVSSIM_ROOT_PATH/$EVSSIM_DIST_FOLDER/$EVSSIM_QEMU_IMAGE
    evssim_qemu $1 "$(evssim_build_ssd_conf)" \
                $EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/$EVSSIM_QEMU_IMAGE \
                $EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_SIMULATOR_FOLDER/infra/ansible/roles/guest_tester_pre/files/bios.bin \
                $EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/kernel/vmlinuz-$EVSSIM_KERNEL_DIST \
                $EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/kernel/initrd.img-$EVSSIM_KERNEL_DIST \
                "root=/dev/sda ro"
}

# Run QEMU attached to console
# Parameters - None
evssim_qemu_attached () {
    evssim_qemu_default attached
}

# Run QEMU detached from console. Use evssim_qemu_stop to stop.
# Parameters - None
evssim_qemu_detached () {
    evssim_qemu_default detached
}

# Use fresh image of qemu
# Parameters - None
evssim_qemu_fresh_image () {
    local IMAGE_PATH=$EVSSIM_ROOT_PATH/$EVSSIM_DIST_FOLDER/$EVSSIM_QEMU_IMAGE
    local IMAGE_PATH_TEMPLATE=$EVSSIM_ROOT_PATH/$EVSSIM_DIST_FOLDER/$EVSSIM_QEMU_IMAGE".template"

    if [ ! -f $IMAGE_PATH ]; then
        echo "ERORR QEMU Image is missing. Please run ./build-qemu-image.sh first."
        exit 1;
    fi

    cp -f $IMAGE_PATH_TEMPLATE $IMAGE_PATH

    # Copy tools inside
    evssim_copy_tools
}

# Copy NVME tools, exofs tool and OSD emulator into the QEMU image
# Copy into an offline image using mounting of the qemu image.
# Parameters - None
# Example
#   evssim_copy_tools
evssim_copy_tools () {
    evssim_run_mounted "mkdir -p guest && cp -Rt guest \
        $EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/nvme \
        $EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/tnvme \
        $EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/dnvme.ko \
        $EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_SIMULATOR_FOLDER/eVSSIM/tests/guest/*"

    # copy the OSD emulator (osc-osd)
    evssim_run_mounted "cp -Rt . $EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/osc-osd"
    evssim_run_mounted "sudo cp $EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/libosd.so $EVSSIM_GUEST_MOUNT_POINT/lib/"
    # copy the mkfs.exofs utility
    evssim_run_mounted "sudo cp $EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/mkfs.exofs \
        $EVSSIM_GUEST_MOUNT_POINT/bin/"
    # copy the script to setup the environment and mount exofs
    evssim_run_mounted "cp -r $EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/exofs ."

    evssim_run_mounted sudo rsync -qrptgo $EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/kernel/lib/ $EVSSIM_GUEST_MOUNT_POINT/lib/
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
    ssh -q $ssh_extra_tty -i $EVSSIM_ROOT_PATH/$EVSSIM_BUILDER_FOLDER/docker/id_rsa -p 2222 -o ConnectionAttempts=1024 -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no -o PasswordAuthentication=no -o PubkeyAcceptedKeyTypes=+ssh-rsa $EVSSIM_QEMU_UBUNTU_USERNAME@localhost bash -c \"$@\"
}

evssim_guest_copy () {
    DOCKET_FILE_PATH=$1
    OUTPUT_FILE_PATH=$2
    scp -r -o ConnectionAttempts=1024 -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no -o PasswordAuthentication=no -o PubkeyAcceptedKeyTypes=+ssh-rsa -i $EVSSIM_ROOT_PATH/$EVSSIM_BUILDER_FOLDER/docker/id_rsa -P 2222 $EVSSIM_QEMU_UBUNTU_USERNAME@localhost:$DOCKET_FILE_PATH $OUTPUT_FILE_PATH
}

