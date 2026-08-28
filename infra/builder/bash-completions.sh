#!/bin/bash

_evssim_build_qemu_image_complete() {
    if [ "$COMP_CWORD" == 1 ]; then
        shopt -s nullglob
        local word="${COMP_WORDS[1]}"
        local versions=(./versions/image-maker/*.sh)
        versions=("${versions[@]##*/}")
        versions=("${versions[@]%.sh}")
        COMPREPLY=($(compgen -W "${versions[*]}" -- "$word"))
    elif [ "$COMP_CWORD" == 2 ]; then
        local word="${COMP_WORDS[2]}"
        local versions="$(docker image ls "$EVSSIM_DOCKER_IMAGE_NAME" --format '{{.Tag}}')"
        COMPREPLY=($(compgen -W "$versions" -- "$word"))
    fi
}

_evssim_docker_enter_complete() {
    if [ "$COMP_CWORD" == 1 ]; then
        local word="${COMP_WORDS[1]}"
        local versions="$(docker ps --format '{{.Image}}' | awk -F: "\$1 == \"$EVSSIM_DOCKER_IMAGE_NAME\" {print \$2}")"
        COMPREPLY=($(compgen -W "$versions" -- "$word"))
    fi
}

_evssim_scripts_complete() {
    if [ "$COMP_CWORD" == 1 ]; then
        local word="${COMP_WORDS[1]}"
        local versions="$(docker image ls "$EVSSIM_DOCKER_IMAGE_NAME" --format '{{.Tag}}')"
        COMPREPLY=($(compgen -W "$versions" -- "$word"))
    fi
}

commands="compile-guest-tests.sh compile-host-tests.sh compile-kernel.sh compile-qemu.sh \
    docker-copy-into-guest.sh docker-enter.sh \
    docker-run-bash.sh docker-run-qemu.sh docker-run-qemu-enter-guest.sh docker-run-sanity.sh \
    docker-test-exofs.sh docker-test-guest.sh docker-test-host-elk.sh"
for command in $commands; do
    complete -F _evssim_scripts_complete "./$command"
done

complete -F _evssim_build_qemu_image_complete ./build-qemu-image.sh
complete -F _evssim_docker_enter_complete ./docker-enter.sh
