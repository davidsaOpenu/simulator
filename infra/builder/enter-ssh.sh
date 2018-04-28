#!/bin/bash
source ./builder.sh

ssh -i ./builder/docker/id_rsa -p 2222 -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no esd@localhost