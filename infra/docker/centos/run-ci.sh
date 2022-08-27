#!/bin/bash

cd ${WORKSPACE}/simulator

echo RUN groupadd `stat -c "%G" .` -g `stat -c "%g" .` >> ${WORKSPACE}/simulator/infra/docker/centos/Dockerfile
echo RUN useradd -ms /bin/bash `stat -c "%U" .` -u `stat -c "%u" .` -g `stat -c "%g" .` -G wheel >> ${WORKSPACE}/simulator/infra/docker/centos/Dockerfile

docker build -t os-centos ${WORKSPACE}/simulator/infra/docker/centos


[ ! -d ${WORKSPACE}/hda/ ] && mkdir ${WORKSPACE}/hda/
ls -all ${WORKSPACE}/hda/
if [[ ! -f ${WORKSPACE}/hda/hda_clean.qcow2 ]]
then
  cp ${WORKSPACE}/../hda/hda_clean.qcow2  ${WORKSPACE}/hda
fi

pushd ${WORKSPACE}/simulator/infra/docker/elk
docker-compose -p evssim-elk-centos up --detach

docker run --rm -u `stat -c "%u:%g" .` -i  -v /tmp/.X11-unix:/tmp/.X11-unix -v ${WORKSPACE}:/code --cap-add SYS_PTRACE --privileged=true os-centos /bin/bash -C "/code/simulator/infra/docker/centos/run-ansible"

docker-compose -p evssim-elk-centos down
popd

# run cppcheck (ignore for now)
#/opt/cppcheck/cppcheck --enable=all --inconclusive --xml --xml-version=2  ${WORKSPACE}/simulator/eVSSIM/QEMU/hw 2> ${WORKSPACE}/cppcheck.xml
