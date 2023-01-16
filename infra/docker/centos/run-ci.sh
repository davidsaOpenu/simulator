#!/bin/bash

export CI_OPERATING_SYSTEM=centos
export SUPERUSER_GROUP=wheel

sleep 2h

${WORKSPACE}/simulator/infra/docker/run-ci.sh