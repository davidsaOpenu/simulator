#!/bin/bash
set -e

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

# Change to project root
cd "$EVSSIM_ROOT_PATH"

# Create output folders (Might already exist)
for folder in $(echo $EVSSIM_CREATE_FOLDERS | tr " " "\n"); do
    if [ ! -d "$folder" ]; then mkdir "$folder"; fi
done

evssim_run_at_folder () {
    evssim_run_at_path $EVSSIM_DOCKER_ROOT_PATH/$1 "${@:2}"
}

evssim_run_at_path () {
    local path=$1
    local args="${@:2}"
    docker run --rm -ti --privileged --env-file <(evssim_all_env) -v $EVSSIM_ROOT_PATH:$EVSSIM_DOCKER_ROOT_PATH $EVSSIM_DOCKER_IMAGE_NAME bash -c "cd $path; $args"
}

evssim_run () {
    evssim_run_at_path $EVSSIM_DOCKER_ROOT_PATH "$@"
}

evssim_script () {
    echo "$@" | evssim_run
}

evssim_run_chrooted () {
    local mount_point=/mnt/image-maker

    # Verify the disk exists
    if [ ! -f $IMAGE_PATH ]; then
        echo "ERROR Missing qemu image file. Run 'build-qemu-image.sh'"
        exit 1
    fi

    # Run inside th chroot
    local mount_point=/mnt/image-maker
    local image_path=$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/$EVSSIM_QEMU_IMAGE
    EVSSIM_RUN_SUDO=y evssim_run "mkdir -p $mount_point && mount -o loop $image_path $mount_point && chroot $mount_point $@"
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

    local args="cd $EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_QEMU_FOLDER/hw && ../x86_64-softmmu/qemu-system-x86_64 -m 4096 -smp 4 -hda $image -device nvme -redir tcp:$EVSSIM_QEMU_PORT::22 -vnc :$EVSSIM_QEMU_VNC -machine accel=kvm -kernel $kernel -initrd $initrd -bios $bios -append '$append'";

    echo $ssd > $EVSSIM_DATA_FOLDER/ssd.conf

    local ports=""
    for i in $(echo $EVSSIM_DOCKER_PORTS | tr -d "\n" | tr "," " "); do
        ports="$ports -p $i"
    done

    echo "QEMU $attached $args"

    case "$attached" in
        attached)
            docker run --rm -ti --privileged --env-file <(evssim_all_env) $ports -v $EVSSIM_ROOT_PATH/$EVSSIM_DATA_FOLDER:$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_QEMU_FOLDER/hw/data -v $EVSSIM_ROOT_PATH:$EVSSIM_DOCKER_ROOT_PATH $EVSSIM_DOCKER_IMAGE_NAME bash -c "cd $path; $args"
            ;;
        *)
            qemu_uuid=$(docker run --rm -d --privileged --env-file <(evssim_all_env) $ports -v $EVSSIM_ROOT_PATH/$EVSSIM_DATA_FOLDER:$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_QEMU_FOLDER/hw/data -v $EVSSIM_ROOT_PATH:$EVSSIM_DOCKER_ROOT_PATH $EVSSIM_DOCKER_IMAGE_NAME bash -c "cd $path; $args")
            qemu_pid=$(docker inspect $qemu_uuid --format "{{.State.Pid}}")
            trap "docker kill $qemu_uuid >/dev/null" EXIT SIGTERM SIGINT
            sleep 1
            ;;
    esac
}

evssim_qemu_wait () {
    docker wait $qemu_uuid
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

evssim_guest () {
    ssh -i $EVSSIM_ROOT_PATH/$EVSSIM_BUILDER_FOLDER/docker/id_rsa -p 2222 -o ConnectionAttempts=1024 -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no esd@localhost "$@"
}