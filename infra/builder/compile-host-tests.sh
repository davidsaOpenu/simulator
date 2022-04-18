#!/bin/bash
source ./builder.sh

#compile osc-osd
evssim_run_at_path $EVSSIM_SIMULATOR_FOLDER/eVSSIM/osc-osd "make target_clean && make target"

# Compile host tests
evssim_run_at_path $EVSSIM_SIMULATOR_FOLDER/eVSSIM/tests/host "make distclean && make mklink && bear make"
