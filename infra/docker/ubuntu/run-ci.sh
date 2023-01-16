#!/bin/bash

export CI_OPERATING_SYSTEM=ubuntu
export SUPERUSER_GROUP=sudo

sleep 10m

${WORKSPACE}/simulator/infra/docker/run-ci.sh