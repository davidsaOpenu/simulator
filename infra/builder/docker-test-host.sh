#!/bin/bash
source ./builder.sh
evssim_run_at_folder $EVSSIM_SIMULATOR_FOLDER/eVSSIM/tests/host ./run_all_host_tests.sh ${@}