#!/bin/bash

set -e

function cleanup_elk() {
  pushd ${WORKSPACE}/simulator/infra/docker/elk
  $DOCKER_COMPOSE_BINARY -p $DOCKER_COMPOSE_PROJECT down
  popd
}


export DOCKER_COMPOSE_PROJECT="evssim-elk-$CI_OPERATING_SYSTEM"
DOCKER_COMPOSE_BINARY=`whereis -b docker-compose | cut -d ' ' -f2`

cd ${WORKSPACE}/simulator

echo RUN groupadd `stat -c "%G" .` -g `stat -c "%g" .` >> ${WORKSPACE}/simulator/infra/docker/$CI_OPERATING_SYSTEM/Dockerfile
echo RUN useradd -ms /bin/bash `stat -c "%U" .` -u `stat -c "%u" .` -g `stat -c "%g" .` -G $SUPERUSER_GROUP >> ${WORKSPACE}/simulator/infra/docker/$CI_OPERATING_SYSTEM/Dockerfile

docker build -t os-$CI_OPERATING_SYSTEM ${WORKSPACE}/simulator/infra/docker/$CI_OPERATING_SYSTEM
docker build -t elk-tests ${WORKSPACE}/simulator/infra/elk/tests

[ ! -d ${WORKSPACE}/hda/ ] && mkdir ${WORKSPACE}/hda/
ls -all ${WORKSPACE}/hda/
if [[ ! -f ${WORKSPACE}/hda/hda_clean.qcow2 ]]
then
  cp ${WORKSPACE}/../hda/hda_clean.qcow2  ${WORKSPACE}/hda
fi

# run elk stack tests
pushd ${WORKSPACE}/simulator/infra/docker/elk

rm -rf ${WORKSPACE}/logs/*
cp -a ${WORKSPACE}/simulator/infra/elk/sample/* ${WORKSPACE}/logs/

trap cleanup_elk EXIT
$DOCKER_COMPOSE_BINARY -p $DOCKER_COMPOSE_PROJECT up --detach
docker run --rm \
    --volume="${WORKSPACE}/logs/:/logs/" \
    --network="$DOCKER_COMPOSE_PROJECT" \
    elk-tests \
    --logs-directory /logs/ \
    --elasticsearch-host elasticsearch \
    --elasticsearch-port 9200 \
    --elasticsearch-index-template filebeat-*

popd

docker run --rm -u `stat -c "%u:%g" .` -i  -v /tmp/.X11-unix:/tmp/.X11-unix -v ${WORKSPACE}:/code --cap-add SYS_PTRACE --privileged=true os-$CI_OPERATING_SYSTEM /bin/bash -C "/code/simulator/infra/docker/$CI_OPERATING_SYSTEM/run-ansible"


# run cppcheck (ignore for now)
#/opt/cppcheck/cppcheck --enable=all --inconclusive --xml --xml-version=2  ${WORKSPACE}/simulator/eVSSIM/QEMU/hw 2> ${WORKSPACE}/cppcheck.xml
