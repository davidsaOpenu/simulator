#!/bin/bash
source ./builder.sh

config_version=${EVSSIM_HOST_TESTS_COMPILE_CONTAINER#ubuntu:}
prog=$(basename $0)
if [ "$#" -gt 1 ]; then
	echo $prog: Error: Too many arguments.
	exit 2
fi
version="${1:-$config_version}"
echo $version

###########################################################################
#            Copy & Restore logging_server.c, logging_server.h            #
#                                                                         #
# This step is crucial because both QEMU compilation and host tests       #
# compilation requires these files. QEMU uses these files while compiling #
# under 14.04 with libwebsockets v4.1, and Host Tests use these files     #
# while compiling under 26.04 with libwebsockets v4.5. The issue is that  #
# These files use libwebsockets and the change from v4.1 to v4.5 required #
# a change in log_server. QEMU won't compile with the new log_server      #
# due to older libwebsockets, Host Tests won't compile with the old       #
# log_server due to newer libwebsockets.                                  #
###########################################################################

# This is a temporary step until full migration to 26.04 happens!
# Copy logging_server.c for 26.04 with libwebsockets 4.5
copy_logging_server() {
	local ver=$1
	mv $EVSSIM_ROOT_PATH/$EVSSIM_SIMULATOR_FOLDER/eVSSIM/MONITOR/SERVER/logging_server.c $EVSSIM_ROOT_PATH/$EVSSIM_SIMULATOR_FOLDER/eVSSIM/MONITOR/SERVER/logging_server.c.ORIGINAL
	mv $EVSSIM_ROOT_PATH/$EVSSIM_SIMULATOR_FOLDER/eVSSIM/MONITOR/SERVER/logging_server.h $EVSSIM_ROOT_PATH/$EVSSIM_SIMULATOR_FOLDER/eVSSIM/MONITOR/SERVER/logging_server.h.ORIGINAL
	cp $EVSSIM_CONTAINERS_FOLDER/$ver/logging_server.c $EVSSIM_ROOT_PATH/$EVSSIM_SIMULATOR_FOLDER/eVSSIM/MONITOR/SERVER/logging_server.c
	cp $EVSSIM_CONTAINERS_FOLDER/$ver/logging_server.h $EVSSIM_ROOT_PATH/$EVSSIM_SIMULATOR_FOLDER/eVSSIM/MONITOR/SERVER/logging_server.h
}

# This is a temporary step until full migration to 26.04 happens!
# Restore Original logging_server.c
restore_logging_server() {
	mv $EVSSIM_ROOT_PATH/$EVSSIM_SIMULATOR_FOLDER/eVSSIM/MONITOR/SERVER/logging_server.c.ORIGINAL $EVSSIM_ROOT_PATH/$EVSSIM_SIMULATOR_FOLDER/eVSSIM/MONITOR/SERVER/logging_server.c || true
	mv $EVSSIM_ROOT_PATH/$EVSSIM_SIMULATOR_FOLDER/eVSSIM/MONITOR/SERVER/logging_server.h.ORIGINAL $EVSSIM_ROOT_PATH/$EVSSIM_SIMULATOR_FOLDER/eVSSIM/MONITOR/SERVER/logging_server.h || true
}


# export EVSSIM_DOCKER_IMAGE_NAME="$EVSSIM_HOST_TESTS_COMPILE_CONTAINER"
export EVSSIM_DOCKER_IMAGE_NAME="$EVSSIM_DOCKER_IMAGE_NAME:$version"

trap "restore_logging_server" EXIT
copy_logging_server $version

evssim_run_at_path $EVSSIM_SIMULATOR_FOLDER/eVSSIM/osc-osd "make target_clean && make target -j`nproc`"

# Compile host tests
evssim_run_at_path $EVSSIM_SIMULATOR_FOLDER/eVSSIM/tests/host "make distclean && make mklink && bear -- make -j`nproc`"

# Compile guest tests
#evssim_run_at_path $EVSSIM_SIMULATOR_FOLDER "make distclean && make mklink && bear make"
