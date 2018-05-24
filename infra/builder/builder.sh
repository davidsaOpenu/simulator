#!/bin/bash
set -e

declare -x EVSSIM_DOCKER_UUID

# Verify environment is loaded
if [ -z $EVSSIM_ENVIRONMENT ]; then
    echo "ERROR Builder not running in evssim environment. Please execute 'source ./env.sh' first"
    exit 1
fi

# Warn if environment changed
if [ -z $EVSSIM_ENV_PATH ]; then
    echo "ERORR Missing environment file path"
    exit 1
elif [[ $(md5sum $EVSSIM_ENV_PATH | cut -d " " -f 1) != $EVSSIM_ENV_HASH ]]; then
    echo "WARNING Environment file hash changed. Please reload using 'source ./env.sh'"
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
    folder_link=$EVSSIM_ROOT_PATH/$EVSSIM_SIMULATOR_FOLDER/$folder
    if [ ! -L $folder_link ]; then
        ln -rs $EVSSIM_ROOT_PATH/$EVSSIM_DATA_FOLDER $folder_link
    fi
done

evssim_run_at_folder () {
    evssim_run_at_path $EVSSIM_DOCKER_ROOT_PATH/$1 "${@:2}"
}

evssim_run_at_path () {
    local path=$1
    local args="${@:2}"
    docker run --rm -i $docker_extra_tty --privileged -p 2222:2222 -p 5900:5900 --env-file <(evssim_all_env) -v $EVSSIM_ROOT_PATH:$EVSSIM_DOCKER_ROOT_PATH $EVSSIM_DOCKER_IMAGE_NAME bash -c "cd $path; $args"
}

evssim_run () {
    evssim_run_at_path $EVSSIM_DOCKER_ROOT_PATH "$@"
}

evssim_script () {
    echo "$@" | evssim_run
}

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

evssim_build_ssd_conf () {
    python -c "import os; import sys; sys.stdout.write(open('$EVSSIM_RUNTIME_SSD_CONF_TEMPLATE', 'rt').read() % os.environ)"
}

evssim_all_env () {
    env | grep "^EVSSIM_"
}

evssim_qemu () {
    local attached=$1
    local ssd=$2
    local image=$3
    local bios=$4
    local kernel=$5
    local initrd=$6
    local append=$7

    local timeout=""
    if [ "$attached" != "attached" ]; then
        timeout="timeout $EVSSIM_DOCKER_MAX_TIMEOUT_IN_MINUTES"m
    fi

    local args="cd $EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_QEMU_FOLDER/hw && qemu-img create -f raw $EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DATA_FOLDER/nvme.img 1g >/dev/null && $timeout ../x86_64-softmmu/qemu-system-x86_64 -trace "nvme*" -m 4096 -smp 4 -drive format=raw,file=$image -drive format=raw,file=$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DATA_FOLDER/nvme.img,if=none,id=memory -device nvme,drive=memory,serial=1 -device e1000,netdev=net0 -netdev user,id=net0,hostfwd=tcp::$EVSSIM_QEMU_PORT-:22 -vnc :$EVSSIM_QEMU_VNC -machine accel=kvm -kernel $kernel -initrd $initrd -L /usr/share/seabios -L ../pc-bios/optionrom -append '$append'";

    local ports=""
    for i in $(echo $EVSSIM_DOCKER_PORTS | tr -d "\n" | tr "," " "); do
        ports="$ports -p $i"
    done

    # Stop any previous runs
    evssim_qemu_stop

    echo "INFO Starting QEMU ($attached) with args = $args"

    # Clear previous runtime data
    if [[ "$EVSSIM_RUNTIME_ALWAYS_RESET" =~ y.* ]]; then
        rm -rf "$EVSSIM_DATA_FOLDER/*"
    fi

    # Build ssd configuration
    echo $ssd > $EVSSIM_DATA_FOLDER/ssd.conf

    case "$attached" in
        attached)
            docker run --rm -i $docker_extra_tty --privileged --env-file <(evssim_all_env) $ports -v $EVSSIM_ROOT_PATH/$EVSSIM_DATA_FOLDER:$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_QEMU_FOLDER/hw/data -v $EVSSIM_ROOT_PATH:$EVSSIM_DOCKER_ROOT_PATH $EVSSIM_DOCKER_IMAGE_NAME bash -c "cd $path; $args"
            ;;
        *)
            export EVSSIM_DOCKER_UUID=$(docker run --rm -d --privileged --env-file <(evssim_all_env) $ports -v $EVSSIM_ROOT_PATH/$EVSSIM_DATA_FOLDER:$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_QEMU_FOLDER/hw/data -v $EVSSIM_ROOT_PATH:$EVSSIM_DOCKER_ROOT_PATH $EVSSIM_DOCKER_IMAGE_NAME bash -c "cd $path; $args")
            echo INFO Docker started $EVSSIM_DOCKER_UUID
            trap "evssim_qemu_stop" EXIT SIGTERM SIGINT
            sleep 1
            ;;
    esac
}

evssim_qemu_stop () {
    if [ ! -z $EVSSIM_DOCKER_UUID ]; then
        if docker ps -q --no-trunc | grep $EVSSIM_DOCKER_UUID > /dev/null; then
            docker stop $EVSSIM_DOCKER_UUID > /dev/null
            echo WARNING Killed docker $EVSSIM_DOCKER_UUID
        fi
        export EVSSIM_DOCKER_UUID=""
    fi
}

evssim_qemu_default () {
    local image_path=$EVSSIM_ROOT_PATH/$EVSSIM_DIST_FOLDER/$EVSSIM_QEMU_IMAGE
    evssim_qemu $1 "$(evssim_build_ssd_conf)" \
                $EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/$EVSSIM_QEMU_IMAGE \
                $EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_SIMULATOR_FOLDER/infra/ansible/roles/guest_tester_pre/files/bios.bin \
                $EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/kernel/vmlinuz-$EVSSIM_KERNEL_DIST \
                $EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/kernel/initrd.img-$EVSSIM_KERNEL_DIST \
                "root=/dev/sda ro"
}

evssim_qemu_attached () {
    evssim_qemu_default attached
}

evssim_qemu_detached () {
    evssim_qemu_default detached
}

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

evssim_copy_tools () {
    evssim_run_mounted "mkdir -p guest && cp -Rt guest \
        $EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/nvme \
        $EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/tnvme \
        $EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/dnvme.ko \
        $EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_SIMULATOR_FOLDER/eVSSIM/tests/guest/*"

    evssim_run_mounted sudo rsync -qrptgo $EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/kernel/lib/ $EVSSIM_GUEST_MOUNT_POINT/lib/
}

evssim_guest () {
    ssh_extra_tty=""
    if [ -t 0 ]; then
        ssh_extra_tty=-t
    fi
    ssh -q $ssh_extra_tty -i $EVSSIM_ROOT_PATH/$EVSSIM_BUILDER_FOLDER/docker/id_rsa -p 2222 -o ConnectionAttempts=1024 -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no -o PasswordAuthentication=no $EVSSIM_QEMU_UBUNTU_USERNAME@localhost bash -c \"$@\"
}