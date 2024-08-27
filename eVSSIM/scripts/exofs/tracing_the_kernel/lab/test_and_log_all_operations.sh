sudo umount /mnt/osd0

echo \n\> Tracing 'mount' operation...
./run.sh mount disabled

echo \n\> Tracing 'open' operation...
./run.sh open disabled

echo \n\> Tracing 'close' operation...
./run.sh close disabled

echo \n\> Tracing 'read' operation...
./run.sh read disabled

echo \n\> Tracing 'write' operation...
./run.sh write disabled

echo All log files can be found at $(pwd)/output
