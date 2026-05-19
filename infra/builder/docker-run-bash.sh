#!/bin/bash
source ./builder.sh

evssim_validate_arguments "$0" "$#"
version="$1"

# Run bash
evssim_run "$version" bash
