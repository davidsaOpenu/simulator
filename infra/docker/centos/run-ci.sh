#!/bin/bash

export CI_OPERATING_SYSTEM=centos
export SUPERUSER_GROUP=wheel

${WORKSPACE}/simulator/infra/docker/run-ci.sh