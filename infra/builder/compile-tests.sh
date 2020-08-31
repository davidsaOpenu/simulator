#!/bin/bash
source ./builder.sh


evssim_run_at_path $EVSSIM_SIMULATOR_FOLDER/eVSSIM/osc-osd "make target_clean && make target"

# Compile host tests
evssim_run_at_path $EVSSIM_SIMULATOR_FOLDER/eVSSIM/tests/host "make distclean && make mklink && bear make"

# Compile guest tests
#evssim_run_at_path $EVSSIM_SIMULATOR_FOLDER "make distclean && make mklink && bear make"
