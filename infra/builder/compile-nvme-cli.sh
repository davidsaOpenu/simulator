#!/bin/bash
source ./builder.sh

progname=$(basename "$0")
valid_versions=("$EVSSIM_NVME_CLI_VERSIONS_FOLDER"/*.sh)
valid_versions=("${valid_versions[@]##*/}")
valid_versions=("${valid_versions[@]%.sh}")

usage() {
    echo Usage: $progname NVME_CLI_VERSION
    echo Valid versions: ${valid_versions[@]}
}

args_error() {
    local error="$1"

    echo $progname: Error: $error
    usage "$progname"
    exit 2
}

validate_args() {
    local version="$1"
    local argc="$2"
    local is_valid_version="false"

    for ver in ${valid_versions[@]}; do
        if [ "$ver" = "$version" ]; then
            is_valid_version="true"
            break
        fi
    done

    if [ "$argc" -ne 1 ]; then
        args_error "Wrong number of arguments."
    elif [ "$is_valid_version" = "false" ]; then
        args_error "Invalid version '$version'."
    fi
}

validate_args "${1:-}" "$#"
version="$1"

echo Building nvme-cli...
source "$EVSSIM_NVME_CLI_VERSIONS_FOLDER/$version.sh"
