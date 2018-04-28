#!/bin/bash
source ./builder.sh

evssim_run_at_path $EVSSIM_SIMULATOR_FOLDER/eVSSIM/MONITOR/SSD_MONITOR_PM "qmake -o Makefile ssd_monitor_p.pro && make"

evssim_run_at_path $EVSSIM_SIMULATOR_FOLDER/eVSSIM/osc-osd "make target_clean && make target"
