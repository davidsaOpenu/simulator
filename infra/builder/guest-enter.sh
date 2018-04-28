#!/bin/bash
source ./builder.sh

ssh -q -i $EVSSIM_BUILDER_FOLDER/docker/id_rsa -p 2222 -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no esd@localhost