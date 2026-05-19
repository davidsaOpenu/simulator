#!/bin/bash
source ./builder.sh

evssim_validate_arguments "$0" "$#"
version="$1"

evssim_run_at_folder "$version" $EVSSIM_SIMULATOR_FOLDER/eVSSIM/tests/host ./run_all_host_tests.sh
