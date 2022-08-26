#!/bin/bash

cd ${WORKSPACE}/simulator

echo RUN groupadd `stat -c "%G" .` -g `stat -c "%g" .` >> ${WORKSPACE}/simulator/infra/docker/ubuntu/Dockerfile 
echo RUN useradd -ms /bin/bash `stat -c "%U" .` -u `stat -c "%u" .` -g `stat -c "%g" .` -G sudo >> ${WORKSPACE}/simulator/infra/docker/ubuntu/Dockerfile

docker build -t os-ubuntu ${WORKSPACE}/simulator/infra/docker/ubuntu

[ ! -d ${WORKSPACE}/hda/ ] && mkdir ${WORKSPACE}/hda/
ls -all ${WORKSPACE}/hda/
if [[ ! -f ${WORKSPACE}/hda/hda_clean.qcow2 ]]
then
  cp ${WORKSPACE}/../hda/hda_clean.qcow2  ${WORKSPACE}/hda
fi

#-u `stat -c "%u:%g" .`
docker run -rm -u `stat -c "%u:%g" .` -i  -v /tmp/.X11-unix:/tmp/.X11-unix -v ${WORKSPACE}:/code --cap-add SYS_PTRACE --privileged=true os-ubuntu /bin/bash -C "/code/simulator/infra/docker/ubuntu/run-ansible"

# run cppcheck (ignore for now)
#/opt/cppcheck/cppcheck --enable=all --inconclusive --xml --xml-version=2  ${WORKSPACE}/simulator/eVSSIM/QEMU/hw 2> ${WORKSPACE}/cppcheck.xml


