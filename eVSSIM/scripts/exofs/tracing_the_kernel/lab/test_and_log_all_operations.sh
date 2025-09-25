#!/bin/bash
sudo umount "${MOUNT_POINT}"

echo \n\> Tracing 'mount' operation...
./run.sh mount disabled

echo \n\> Tracing 'open' operation...
./run.sh open disabled

echo -e "\n> Tracing 'ls' operation..."
./run.sh ls disabled

# comment out close, read, write operations because they are not supported yet by exofs on nvme backend
# since some of the function still going to osd this causes kernel panic
# TODO: uncomment these operations when they are supported

# echo \n\> Tracing 'close' operation...
# ./run.sh close disabled

# echo \n\> Tracing 'read' operation...
# ./run.sh read disabled

# echo \n\> Tracing 'write' operation...
# ./run.sh write disabled

echo All log files can be found at $(pwd)/output
