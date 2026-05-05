#!/bin/bash

check_tool() {
    local tool="$1"
    local alt="$2"
    local found

    if command -v "$tool" &>/dev/null; then
        found="$tool"
    elif [ -n "$alt" ] && command -v "$alt" &>/dev/null; then
        found="$alt"
    else
        local msg="$tool"
        [ -n "$alt" ] && msg="$tool or $alt"
        echo "Failed. Please install $msg — NOT installed"
        exit 1
    fi

    local version
    version=$("$found" --version 2>&1 | head -n1)
    echo "Success. $found — $version"
}

check_tools() {
    check_tool "ansible"
    check_tool "tox"
    check_tool "docker" "podman"
    check_tool "docker-compose" "podman-compose"
}
