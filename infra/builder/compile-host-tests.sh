#!/bin/bash
source ./builder.sh

evssim_validate_version_arguments "$0" "${1:-}" "$#"
version="$1"

# Compile OSC-OSD
evssim_run_at_path "$version" $EVSSIM_SIMULATOR_FOLDER/eVSSIM/osc-osd "make target_clean && make target"

# Compile host tests
evssim_run_at_path "$version" $EVSSIM_SIMULATOR_FOLDER/eVSSIM/tests/host "make distclean && make mklink && bear make"
