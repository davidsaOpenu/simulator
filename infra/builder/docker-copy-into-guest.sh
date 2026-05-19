#!/bin/bash
source ./builder.sh

evssim_validate_arguments "$0" "$#"
version="$1"

evssim_qemu_fresh_image "$version"
